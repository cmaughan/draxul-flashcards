#include "review.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <regex>
#include <set>
#include <stdexcept>

namespace flashcards
{
namespace
{
using Json = nlohmann::json;
constexpr int64_t max_time = 4'000'000'000'000;
Json bounded_json(std::string_view text)
{
    if (text.size() > 1024 * 1024)
        throw std::runtime_error("Review document exceeds 1 MiB");
    std::map<int, std::set<std::string>> keys;
    return Json::parse(text.begin(), text.end(), [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 16)
            throw std::runtime_error("Review document is too deeply nested");
        if (event == Json::parse_event_t::object_start)
            keys[depth + 1].clear();
        if (event == Json::parse_event_t::key && !keys[depth].insert(value.get<std::string>()).second)
            throw std::runtime_error("Duplicate document key");
        return true;
    });
}
int64_t integer(const Json& obj, const char* name, int64_t high)
{
    const auto& value = obj.at(name);
    if (!value.is_number_integer() || (value.is_number_unsigned() && value.get<uint64_t>() > static_cast<uint64_t>(high)))
        throw std::runtime_error("Invalid review integer");
    const auto number = value.get<int64_t>();
    if (number < 0 || number > high)
        throw std::runtime_error("Review value outside bounds");
    return number;
}
bool valid_id(const std::string& id)
{
    static const std::regex pattern("^[a-z0-9][a-z0-9._-]{0,95}$");
    return std::regex_match(id, pattern);
}
} // namespace

int64_t unix_now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::vector<Card> parse_deck(std::string_view text)
{
    const auto doc = bounded_json(text);
    if (!doc.is_object() || doc.size() != 3 || integer(doc, "schema_version", 3) != 3
        || doc.at("kind") != "bidirectional" || !doc.at("cards").is_array() || doc.at("cards").size() > 2000)
        throw std::runtime_error("Unsupported embedded deck");
    std::vector<Card> cards;
    std::set<std::string> ids;
    for (const auto& value : doc.at("cards"))
    {
        if (!value.is_object() || value.size() != 9)
            throw std::runtime_error("Invalid embedded card");
        Card card{ value.at("id").get<std::string>(), value.at("kana").get<std::string>(),
            value.at("romaji").get<std::string>(), value.at("image").get<std::string>(),
            value.at("attribution").get<std::string>(), value.at("audio").get<std::string>(),
            value.at("audio_attribution").get<std::string>() };
        const auto cue = value.at("cue").get<std::string>();
        card.cue_subject = value.at("cue_subject").get<std::string>();
        if (cue == "picture") card.cue = VisualCue::Picture;
        else if (cue == "listener-reference") card.cue = VisualCue::ListenerReference;
        else if (cue == "subject-marker") card.cue = VisualCue::SubjectMarker;
        else if (cue == "approval-reaction") card.cue = VisualCue::ApprovalReaction;
        else throw std::runtime_error("Unknown visual cue");
        if ((card.cue == VisualCue::SubjectMarker && card.cue_subject != "それ")
            || (card.cue != VisualCue::SubjectMarker && !card.cue_subject.empty())
            || (card.cue != VisualCue::Picture && !card.image.empty()))
            throw std::runtime_error("Invalid visual cue fields");
        if ((card.cue == VisualCue::SubjectMarker && card.kana != "が")
            || (card.cue == VisualCue::ListenerReference && card.kana != "それ")
            || (card.cue == VisualCue::ApprovalReaction && card.kana != "いいね"))
            throw std::runtime_error("Native cue does not match its spelling");
        if (!valid_id(card.id) || !ids.insert(card.id).second || card.kana.empty()
            || card.kana.size() > 192 || card.romaji.size() > 256 || card.attribution.size() > 512
            || card.audio_attribution.size() > 512
            || (!card.image.empty() && !std::regex_match(card.image, std::regex("[a-z0-9_-]+\\.(png|jpg|jpeg)")))
            || (!card.audio.empty() && !std::regex_match(card.audio, std::regex("[a-z0-9_-]+\\.wav"))))
            throw std::runtime_error("Invalid embedded card fields");
        cards.push_back(std::move(card));
        card = cards.back();
        card.direction = Direction::Production;
        cards.push_back(std::move(card));
    }
    for (const auto& card : cards)
        if (!card.cue_subject.empty() && std::none_of(cards.begin(), cards.end(),
                [&](const Card& subject) { return subject.kana == card.cue_subject; }))
            throw std::runtime_error("Native cue subject is outside the deck");
    return cards;
}

State parse_state(std::string_view text)
{
    const auto doc = bounded_json(text);
    if (!doc.is_object() || doc.size() != 2 || integer(doc, "schema_version", 1) != 1
        || !doc.at("cards").is_object() || doc.at("cards").size() > 4000)
        throw std::runtime_error("Unsupported review state");
    State result;
    for (const auto& [id, value] : doc.at("cards").items())
    {
        const auto colon = id.find(':');
        if (colon == std::string::npos || (id.substr(0, colon) != "recognition" && id.substr(0, colon) != "production")
            || !valid_id(id.substr(colon + 1)) || !value.is_object() || value.size() != 7)
            throw std::runtime_error("Invalid review record");
        Progress p;
        p.stage = static_cast<int>(integer(value, "stage", 5));
        p.due = integer(value, "due", max_time);
        p.last_reviewed = integer(value, "last_reviewed", max_time);
        p.reviews = static_cast<int>(integer(value, "reviews", 1'000'000));
        p.remembered = static_cast<int>(integer(value, "remembered", 1'000'000));
        p.forgotten = static_cast<int>(integer(value, "forgotten", 1'000'000));
        p.assisted = static_cast<int>(integer(value, "assisted", 1'000'000));
        if (p.reviews != p.remembered + p.forgotten + p.assisted || p.due < p.last_reviewed)
            throw std::runtime_error("Inconsistent review record");
        result.emplace(id, p);
    }
    return result;
}

std::string serialize_state(const State& state)
{
    Json doc{ { "schema_version", 1 }, { "cards", Json::object() } };
    for (const auto& [key, p] : state)
        doc["cards"][key] = { { "stage", p.stage }, { "due", p.due }, { "last_reviewed", p.last_reviewed },
            { "reviews", p.reviews }, { "remembered", p.remembered }, { "forgotten", p.forgotten }, { "assisted", p.assisted } };
    auto text = doc.dump();
    // Bound retained history too: never emit a state we cannot reload.
    (void)parse_state(text);
    return text;
}

ReviewSession::ReviewSession(std::vector<Card> cards, Read read, Save save, Clock clock, Lock lock)
    : cards_(std::move(cards)), read_(std::move(read)), save_(std::move(save)), clock_(std::move(clock)), lock_(std::move(lock))
{
    refresh();
}
std::string ReviewSession::key(const Card& card) const
{
    return (card.direction == Direction::Recognition ? "recognition:" : "production:") + card.id;
}
const Card* ReviewSession::current() const { return current_ ? &cards_[*current_] : nullptr; }

void ReviewSession::refresh()
{
    try
    {
        const auto text = read_();
        state_ = text ? parse_state(*text) : State{};
        blocked_ = false;
        error_.clear();
        if (!current_)
            select();
    }
    catch (...)
    {
        blocked_ = true;
        current_.reset();
        error_ = "Review state unavailable or invalid. It has been preserved; grading is blocked.";
    }
}

void ReviewSession::select()
{
    current_.reset();
    revealed_ = image_ready_ = false;
    if (batch_finished())
        return;
    int64_t earliest = max_time;
    for (size_t i = 0; i < cards_.size(); ++i)
    {
        if (std::find(skipped_.begin(), skipped_.end(), key(cards_[i])) != skipped_.end())
            continue;
        const auto entry = state_.find(key(cards_[i]));
        const auto due = entry == state_.end() ? 0 : entry->second.due;
        if (due <= clock_() && (!current_ || due < earliest))
        {
            earliest = due;
            current_ = i;
            expected_reviews_ = entry == state_.end() ? 0 : entry->second.reviews;
        }
    }
}
bool ReviewSession::flip()
{
    if (!current() || revealed_ || blocked_)
        return false;
    revealed_ = true;
    return true;
}
void ReviewSession::skip()
{
    if (const auto* card = current())
        skipped_.push_back(key(*card));
    select();
}
void ReviewSession::start_batch()
{
    batch_grades_ = 0;
    skipped_.clear();
    current_.reset();
    refresh();
}
bool ReviewSession::grade(bool remembered)
{
    if (!current() || !revealed_ || blocked_ || !image_ready_)
        return false;
    try
    {
        const auto guard = lock_ ? lock_() : std::shared_ptr<void>{};
        const auto text = read_();
        State next = text ? parse_state(*text) : State{};
        const auto card_key = key(*current());
        auto& p = next[card_key];
        if (p.reviews != expected_reviews_ || p.due > clock_())
        {
            state_ = std::move(next);
            select();
            error_ = "This card was already graded in another pane. Its saved schedule was kept.";
            return false;
        }
        const auto now = clock_();
        if (now < 0 || now > max_time - 30 * 86400)
            throw std::runtime_error("Clock outside bounds");
        ++p.reviews;
        p.last_reviewed = now;
        if (!remembered)
        {
            ++p.forgotten;
            p.stage = 0;
            p.due = now + 600;
        }
        else
        {
            constexpr int days[] = { 1, 3, 7, 14, 30, 30 };
            ++p.remembered;
            p.due = now + days[p.stage] * 86400;
            p.stage = std::min(p.stage + 1, 5);
        }
        save_(serialize_state(next));
        state_ = std::move(next);
        ++batch_grades_;
        error_.clear();
        select();
        return true;
    }
    catch (...)
    {
        error_ = "Grade was not saved. Review state was preserved; retry or reopen after fixing storage.";
        return false;
    }
}
size_t ReviewSession::due_count() const
{
    return std::count_if(cards_.begin(), cards_.end(), [&](const Card& card) {
        const auto entry = state_.find(key(card));
        return entry == state_.end() || entry->second.due <= clock_();
    });
}
std::optional<int64_t> ReviewSession::next_due() const
{
    std::optional<int64_t> result;
    for (const auto& card : cards_)
    {
        const auto entry = state_.find(key(card));
        if (entry != state_.end() && (!result || entry->second.due < *result))
            result = entry->second.due;
    }
    return result;
}
} // namespace flashcards
