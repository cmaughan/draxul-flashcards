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
AudioClip parse_clip(const Json& value, bool private_cache)
{
    if (!value.is_object() || value.size() != (private_cache ? 9 : 4))
        throw std::runtime_error("Invalid pronunciation metadata");
    AudioClip clip{value.at("file").get<std::string>(), value.at("attribution").get<std::string>(),
        value.at("speaker").get<std::string>(), value.at("synthetic").get<bool>(), private_cache};
    if (!std::regex_match(clip.file, std::regex("[a-z0-9_-]+\\.wav")) || clip.attribution.empty()
        || clip.attribution.size() > 512 || clip.speaker.empty() || clip.speaker.size() > 96)
        throw std::runtime_error("Invalid pronunciation fields");
    if (private_cache)
    {
        if (clip.synthetic || value.at("permission") != "personal-use-authorized")
            throw std::runtime_error("Private human audio requires explicit personal-use authorization");
        for (const auto* field : {"source", "license", "changes", "quality"})
        {
            const auto text = value.at(field).get<std::string>();
            if (text.empty() || text.size() > 512)
                throw std::runtime_error("Private pronunciation needs provenance and quality notes");
        }
    }
    return clip;
}
std::vector<AudioClip> parse_clips(const Json& value, bool private_cache)
{
    if (!value.is_array() || value.size() > 8)
        throw std::runtime_error("At most eight pronunciations per word");
    std::vector<AudioClip> clips;
    std::set<std::string> files, speakers;
    for (const auto& item : value)
    {
        auto clip = parse_clip(item, private_cache);
        if (!files.insert(clip.file).second || !speakers.insert(clip.speaker).second)
            throw std::runtime_error("Pronunciations must have distinct files and stated speakers");
        clips.push_back(std::move(clip));
    }
    if (clips.size() > 1 && std::any_of(clips.begin(), clips.end(), [](const auto& c){return c.synthetic;}))
        throw std::runtime_error("Synthetic fallback cannot be mixed into human speaker cycling");
    return clips;
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
    if (!doc.is_object() || doc.size() != 3 || integer(doc, "schema_version", 5) != 5
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
            value.at("attribution").get<std::string>(), parse_clips(value.at("audio"), false) };
        const auto cue = value.at("cue").get<std::string>();
        card.cue_subject = value.at("cue_subject").get<std::string>();
        card.meaning = value.at("meaning").get<std::string>();
        if (card.meaning.empty() || card.meaning.size() > 512
            || std::any_of(card.meaning.begin(), card.meaning.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            throw std::runtime_error("Invalid answer description");
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
            || (!card.image.empty() && !std::regex_match(card.image, std::regex("[a-z0-9_-]+\\.(png|jpg|jpeg)"))))
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

std::map<std::string, std::vector<AudioClip>> parse_private_audio(std::string_view text, const std::vector<Card>& cards)
{
    const auto doc = bounded_json(text);
    if (!doc.is_object() || doc.size() != 2 || integer(doc, "schema_version", 1) != 1
        || !doc.at("entries").is_object() || doc.at("entries").size() > 2000)
        throw std::runtime_error("Unsupported private audio manifest");
    std::map<std::string, std::vector<AudioClip>> selected;
    for (const auto& [key, entry] : doc.at("entries").items())
    {
        if (!valid_id(key) || !entry.is_object() || entry.size() != 2 || !entry.at("kana").is_string())
            throw std::runtime_error("Invalid private audio entry");
        auto clips = parse_clips(entry.at("clips"), true);
        const auto card = std::find_if(cards.begin(), cards.end(), [&](const auto& c){return c.id == key;});
        if (card != cards.end())
        {
            if (entry.at("kana") != card->kana) throw std::runtime_error("Private pronunciation spelling mismatch");
            if (!clips.empty()) selected.emplace(key, std::move(clips));
        }
    }
    return selected;
}
size_t next_audio_index(size_t current, size_t count)
{
    return count ? (current % count + 1) % count : 0;
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

Progress ReviewSession::saved_progress(const Card& card, const State& state, bool opposite) const
{
    const auto card_key = opposite
        ? (card.direction == Direction::Recognition ? "production:" : "recognition:") + card.id : key(card);
    const auto entry = state.find(card_key);
    return entry == state.end() ? Progress{} : entry->second;
}

void ReviewSession::refresh(bool preserve_error)
{
    try
    {
        const auto text = read_();
        state_ = text ? parse_state(*text) : State{};
        if (!preserve_error || blocked_) error_.clear();
        blocked_ = false;
        if (current_ && !revealed_
            && ((!review_ahead_ && eligible_due(cards_[*current_], state_) > clock_())
                || saved_progress(*current(), state_) != expected_progress_
                || (review_ahead_ && saved_progress(*current(), state_, true) != expected_other_progress_)))
            current_.reset();
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
    const auto now = clock_();
    for (size_t i : ordered_indices(now))
    {
        const auto due = eligible_due(cards_[i], state_);
        if (review_ahead_ || due <= now)
        {
            current_ = i;
            expected_progress_ = saved_progress(cards_[i], state_);
            expected_other_progress_ = saved_progress(cards_[i], state_, true);
            break;
        }
    }
}

int64_t ReviewSession::eligible_due(const Card& card, const State& state) const
{
    const auto entry = state.find(key(card));
    int64_t due = entry == state.end() ? 0 : entry->second.due;
    // A grade exposes the same answer in either direction. Derive the shared
    // cooldown from durable records without changing either direction's due.
    for (const auto* direction : { "recognition:", "production:" })
    {
        const auto other = state.find(direction + card.id);
        if (other != state.end() && other->second.reviews > 0)
            due = std::max(due, other->second.last_reviewed + 600);
    }
    return due;
}

std::vector<size_t> ReviewSession::ordered_indices(int64_t now) const
{
    std::vector<size_t> indices;
    for (size_t i = 0; i < cards_.size(); ++i)
        if (std::find(skipped_.begin(), skipped_.end(), key(cards_[i])) == skipped_.end())
            indices.push_back(i);
    const auto due = [&](size_t i) {
        return eligible_due(cards_[i], state_);
    };
    const auto priority = [&](size_t i) {
        const auto entry = state_.find(key(cards_[i]));
        if (entry == state_.end() || entry->second.reviews == 0) return 2;
        // Again resets stage to zero; one explicit Remembered clears priority.
        return entry->second.stage == 0 && entry->second.forgotten > 0 ? 0 : 1;
    };
    std::stable_sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        const auto a_due = due(a), b_due = due(b);
        const bool a_ready = a_due <= now, b_ready = b_due <= now;
        if (a_ready != b_ready) return a_ready;
        if (a_ready && priority(a) != priority(b)) return priority(a) < priority(b);
        return a_due < b_due;
    });
    return indices;
}

std::vector<QueuedCard> ReviewSession::queue() const
{
    std::vector<QueuedCard> result;
    if (blocked_) return result;
    const auto now = clock_();
    auto indices = ordered_indices(now);
    // The shown card stays first even if the clock crosses another due boundary.
    if (current_)
    {
        const auto found = std::find(indices.begin(), indices.end(), *current_);
        if (found != indices.end()) std::rotate(indices.begin(), found, found + 1);
    }
    for (const auto i : indices)
    {
        const int64_t due = eligible_due(cards_[i], state_);
        result.push_back({ &cards_[i], due, due <= now, current_ && i == *current_ });
    }
    return result;
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
void ReviewSession::start_batch(bool review_ahead)
{
    review_ahead_ = review_ahead;
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
        const auto now = clock_();
        if (p != expected_progress_
            || (review_ahead_ && saved_progress(*current(), next, true) != expected_other_progress_)
            || (!review_ahead_ && eligible_due(*current(), next) > now))
        {
            state_ = std::move(next);
            select();
            error_ = "New review results changed this card. Its saved schedule was kept; the stale grade was not applied.";
            return false;
        }
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
        if (review_ahead_) skipped_.push_back(card_key);
        ++batch_grades_;
        error_.clear();
        select();
        return true;
    }
    catch (...)
    {
        error_ = "Grade save could not be confirmed. Retry or reopen after fixing storage; saved progress is preserved.";
        return false;
    }
}
size_t ReviewSession::due_count() const
{
    if (blocked_) return 0;
    const auto now = clock_();
    return std::count_if(cards_.begin(), cards_.end(), [&](const Card& card) {
        return eligible_due(card, state_) <= now;
    });
}
std::optional<int64_t> ReviewSession::next_due() const
{
    std::optional<int64_t> result;
    if (blocked_) return result;
    for (const auto& card : cards_)
    {
        const auto due = eligible_due(card, state_);
        if (!result || due < *result) result = due;
    }
    return result;
}
} // namespace flashcards
