#include "review_export.h"

#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <random>
#include <regex>
#include <set>
#include <stdexcept>
#include <tuple>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace flashcards
{
namespace
{
using Json = nlohmann::json;
constexpr size_t max_pending = 512, max_document = 1024 * 1024;
bool uuid(const std::string& value)
{
    static const std::regex pattern("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}");
    return std::regex_match(value, pattern);
}
Json document(std::string_view value, size_t limit = max_document)
{
    if (value.empty() || value.size() > limit) throw std::runtime_error("Review export document outside size bounds");
    std::map<int, std::set<std::string>> keys;
    return Json::parse(value.begin(), value.end(), [&](int depth, Json::parse_event_t event, Json& item) {
        if (depth > 16) throw std::runtime_error("Review export document too deep");
        if (event == Json::parse_event_t::object_start) keys[depth + 1].clear();
        if (event == Json::parse_event_t::key && !keys[depth].insert(item.get<std::string>()).second)
            throw std::runtime_error("Duplicate review export field");
        return true;
    });
}
int64_t number(const Json& value, const char* field, int64_t maximum)
{
    const auto& item = value.at(field);
    if (!item.is_number_integer() || (item.is_number_unsigned() && item.get<uint64_t>() > static_cast<uint64_t>(maximum)))
        throw std::runtime_error("Invalid review export integer");
    const auto result = item.get<int64_t>();
    if (result < 0 || result > maximum) throw std::runtime_error("Review export integer outside bounds");
    return result;
}
std::string utc(int64_t seconds)
{
    const auto time = static_cast<time_t>(seconds);
    std::tm parts{};
#ifdef _WIN32
    if (gmtime_s(&parts, &time) != 0) throw std::runtime_error("Review export clock unavailable");
#else
    if (!gmtime_r(&time, &parts)) throw std::runtime_error("Review export clock unavailable");
#endif
    std::array<char, 64> text{};
    if (!std::strftime(text.data(), text.size(), "%Y-%m-%dT%H:%M:%SZ", &parts))
        throw std::runtime_error("Review export clock unavailable");
    return text.data();
}
std::string event_key(const Json& event)
{
    return event.at("direction").get<std::string>() + ":" + event.at("word_id").get<std::string>();
}
void validate_event(const Json& event, const std::string& producer)
{
    static const std::regex word_pattern("[a-z0-9][a-z0-9._-]{0,95}");
    if (!event.is_object() || event.size() != 14 || number(event, "schema_version", 1) != 1
        || event.at("kind") != "draxul.flashcard-review" || event.at("producer_id") != producer
        || !uuid(event.at("event_id").get<std::string>()) || !uuid(producer)
        || !uuid(event.at("base_checkpoint_id").get<std::string>())
        || !std::regex_match(event.at("word_id").get<std::string>(), word_pattern)
        || (event.at("direction") != "recognition" && event.at("direction") != "production")
        || (event.at("outcome") != "Again" && event.at("outcome") != "Remembered")
        || !event.at("explicit").is_boolean() || event.at("explicit") != true
        || event.at("source") != Json({{"product", "dev.draxul.flashcards"}, {"version", "0.1.0"}, {"type", "self-report"}})
        || number(event, "producer_sequence", 1'000'000'000) == 0
        || number(event, "local_review_number", 1'000'000) == 0
        || event.at("reviewed_at_utc") != utc(number(event, "reviewed_at_unix", 4'000'000'000'000)))
        throw std::runtime_error("Invalid explicit review export event");
}
State validate_checkpoint(const Json& checkpoint)
{
    if (!checkpoint.is_object() || checkpoint.size() != 7 || number(checkpoint, "schema_version", 1) != 1
        || checkpoint.at("kind") != "draxul.flashcard-checkpoint"
        || !uuid(checkpoint.at("checkpoint_id").get<std::string>())
        || !uuid(checkpoint.at("producer_id").get<std::string>())
        || checkpoint.at("created_at_utc") != utc(number(checkpoint, "created_at_unix", 4'000'000'000'000)))
        throw std::runtime_error("Invalid review bootstrap checkpoint");
    return parse_state(checkpoint.at("recall").dump());
}
void validate_summary(const Json& summary, const std::string& producer, int64_t now)
{
    if (!summary.is_object() || summary.size() != 10 || number(summary, "schema_version", 1) != 1
        || summary.at("kind") != "draxul.flashcard-summary" || summary.at("producer_id") != producer
        || number(summary, "generated_at_unix", 4'000'000'000'000) > now + 300
        || summary.at("generated_at_utc") != utc(summary.at("generated_at_unix").get<int64_t>())
        || number(summary, "latest_limit", 512) != 512 || !summary.at("latest").is_object()
        || summary["latest"].size() > 512)
        throw std::runtime_error("Invalid disposable review summary");
    (void)number(summary, "event_count", 1'000'000'000);
    (void)parse_state(summary.at("schedule").dump());
    const auto& sync = summary.at("sync");
    if (!sync.is_object() || sync.size() != 5 || !sync.at("read_complete").is_boolean()
        || !sync.at("bootstrap_alternatives").is_boolean())
        throw std::runtime_error("Invalid disposable review sync status");
    (void)number(sync, "events_seen", 20'000); (void)number(sync, "checkpoints_seen", 256);
    (void)number(sync, "unresolved_events", 20'000);
    for (const auto& [key, event] : summary["latest"].items())
    {
        validate_event(event, producer);
        if (key != event_key(event)) throw std::runtime_error("Invalid disposable review summary key");
    }
}
Json record(const State& state, const std::string& key)
{
    const auto found = state.find(key);
    return found == state.end() ? Json(nullptr) : Json::parse(serialize_state({{key, found->second}}))["cards"][key];
}
Progress progress(const Json& value, const std::string& key)
{
    if (value.is_null()) return {};
    return parse_state(Json({{"schema_version", 1}, {"cards", {{key, value}}}}).dump()).at(key);
}
std::string read_file(const std::filesystem::path& path, size_t limit)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Shared review file unavailable");
    const auto size = file.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(limit)) throw std::runtime_error("Shared review file outside size bounds");
    std::string data(static_cast<size_t>(size), '\0');
    file.seekg(0);
    if (!file.read(data.data(), static_cast<std::streamsize>(size))) throw std::runtime_error("Shared review file unreadable");
    return data;
}
void durable_directory(const std::filesystem::path& path)
{
#ifndef _WIN32
    const int descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) throw std::runtime_error("Shared review directory flush unavailable");
    const int result = fsync(descriptor);
    close(descriptor);
    if (result != 0) throw std::runtime_error("Shared review directory flush failed");
#else
    (void)path; // MoveFileExW uses MOVEFILE_WRITE_THROUGH below.
#endif
}
void write_file(const std::filesystem::path& path, const Json& value, bool immutable)
{
    if (immutable && std::filesystem::exists(path))
    {
        if (document(read_file(path, max_document)) != value)
            throw std::runtime_error("Shared event ID conflict; existing history preserved");
        durable_directory(path.parent_path());
        return;
    }
    const auto data = value.dump();
    const auto temporary = path.parent_path() / ("." + new_review_id() + ".tmp");
#ifdef _WIN32
    auto* file = _wfopen(temporary.c_str(), L"wb");
#else
    auto* file = std::fopen(temporary.c_str(), "wb");
#endif
    if (!file) throw std::runtime_error("Shared review write unavailable");
    bool complete = std::fwrite(data.data(), 1, data.size(), file) == data.size() && std::fflush(file) == 0;
#ifdef _WIN32
    complete = complete && _commit(_fileno(file)) == 0;
#else
    complete = complete && fsync(fileno(file)) == 0;
#endif
    complete = std::fclose(file) == 0 && complete;
    try
    {
        if (!complete) throw std::runtime_error("Shared review write incomplete");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | (immutable ? 0 : MOVEFILE_REPLACE_EXISTING)))
        {
            if (!immutable || !std::filesystem::exists(path) || document(read_file(path, max_document)) != value)
                throw std::runtime_error("Shared review publish failed or conflicted");
        }
#else
        if (immutable)
        {
            if (link(temporary.c_str(), path.c_str()) != 0
                && (!std::filesystem::exists(path) || document(read_file(path, max_document)) != value))
                throw std::runtime_error("Shared review publish failed or conflicted");
        }
        else std::filesystem::rename(temporary, path);
#endif
        durable_directory(path.parent_path());
    }
    catch (...)
    {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
    std::error_code error;
    std::filesystem::remove(temporary, error);
}
}

std::string new_review_id()
{
    std::random_device random;
    std::array<unsigned char, 16> bytes{};
    for (auto& byte : bytes) byte = static_cast<unsigned char>(random());
    bytes[6] = static_cast<unsigned char>((bytes[6] & 15) | 64);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 63) | 128);
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10) result += '-';
        result += hex[bytes[i] >> 4]; result += hex[bytes[i] & 15];
    }
    return result;
}

SharedHistory rebuild_review_history(const std::vector<std::string>& checkpoint_texts,
    const std::vector<std::string>& event_texts, int64_t now)
{
    if (checkpoint_texts.size() > 256 || event_texts.size() > 20'000)
        throw std::runtime_error("Shared history read limit reached; cached schedule preserved");
    SharedHistory result;
    std::map<std::string, Json> checkpoints, events;
    std::map<std::string, std::string> baseline_ids;
    for (const auto& text : checkpoint_texts)
    {
        const auto checkpoint = document(text);
        const auto state = validate_checkpoint(checkpoint);
        const auto id = checkpoint["checkpoint_id"].get<std::string>();
        if (checkpoint["created_at_unix"].get<int64_t>() > now + 300)
            throw std::runtime_error("Shared bootstrap clock is ahead; cached schedule preserved");
        if (checkpoints.contains(id))
        {
            if (checkpoints.at(id) != checkpoint) throw std::runtime_error("Shared checkpoint ID conflict; history preserved");
            continue;
        }
        checkpoints.emplace(id, checkpoint);
        for (const auto& [key, p] : state)
        {
            const auto existing = result.state.find(key);
            if (existing != result.state.end() && existing->second != p) result.alternative_bootstraps = true;
            if (existing == result.state.end() || std::tuple(p.reviews, p.last_reviewed, id)
                    > std::tuple(existing->second.reviews, existing->second.last_reviewed, baseline_ids[key]))
            {
                result.state[key] = p;
                baseline_ids[key] = id;
            }
        }
    }
    const State baseline = result.state;
    std::map<std::pair<std::string, int64_t>, std::string> sequences;
    for (const auto& text : event_texts)
    {
        const auto event = document(text, 8192);
        validate_event(event, event.at("producer_id"));
        if (event["reviewed_at_unix"].get<int64_t>() > now + 300)
            throw std::runtime_error("Shared grade clock is ahead; cached schedule preserved");
        const auto id = event["event_id"].get<std::string>();
        if (events.contains(id))
        {
            if (events.at(id) != event) throw std::runtime_error("Shared event ID conflict; history preserved");
            continue;
        }
        const auto sequence = std::make_pair(event["producer_id"].get<std::string>(), event["producer_sequence"].get<int64_t>());
        if (sequences.contains(sequence) && sequences.at(sequence) != id)
            throw std::runtime_error("Shared producer sequence conflict; history preserved");
        sequences[sequence] = id;
        events.emplace(id, event);
    }
    std::vector<Json> ordered;
    for (const auto& [id, event] : events)
    {
        const auto checkpoint = checkpoints.find(event["base_checkpoint_id"].get<std::string>());
        if (checkpoint == checkpoints.end()) { ++result.unresolved; continue; }
        if (checkpoint->second["producer_id"] != event["producer_id"])
            throw std::runtime_error("Shared grade refers to another producer's bootstrap");
        ordered.push_back(event);
    }
    // At an identical timestamp Remembered is applied first, then Again. Distinct
    // offline reviews both count, and a same-time conflict ends in relearning.
    std::sort(ordered.begin(), ordered.end(), [](const Json& a, const Json& b) {
        return std::tuple(a["reviewed_at_unix"].get<int64_t>(), a["outcome"] == "Again", a["event_id"].get<std::string>())
            < std::tuple(b["reviewed_at_unix"].get<int64_t>(), b["outcome"] == "Again", b["event_id"].get<std::string>());
    });
    for (const auto& event : ordered)
    {
        const auto key = event_key(event);
        const auto time = event["reviewed_at_unix"].get<int64_t>();
        const auto past = baseline.find(key);
        // An aggregate checkpoint is authoritative through this direction's last
        // review. Its past is preserved as a snapshot, never fabricated as events.
        if (past != baseline.end() && time <= past->second.last_reviewed) continue;
        auto& p = result.state[key];
        ++p.reviews;
        p.last_reviewed = time;
        if (event["outcome"] == "Again") { ++p.forgotten; p.stage = 0; p.due = time + 600; }
        else
        {
            constexpr int days[] = {1, 3, 7, 14, 30, 30};
            ++p.remembered; p.due = time + days[p.stage] * 86400; p.stage = std::min(p.stage + 1, 5);
        }
    }
    (void)serialize_state(result.state); // Serialization also validates current recall bounds.
    result.events = events.size(); result.checkpoints = checkpoints.size();
    return result;
}

struct ReviewExport::Impl
{
    std::filesystem::path directory, cache_directory;
    ReviewSession::Read read_outbox, read_recall;
    ReviewSession::Save save_outbox, save_recall;
    ReviewSession::Clock clock;
    Id id;
    size_t waiting = 0, delivered = 0;
    bool cache_loaded = false, more_to_import = false;
    std::map<std::string, Json> cached_checkpoints, cached_events;
    std::set<std::string> seen_shared;
    SharedHistory history;
    std::string failure;
    State recall() { const auto text = read_recall(); return text ? parse_state(*text) : State{}; }
    Json load()
    {
        const auto text = read_outbox();
        if (!text)
        {
            if (std::filesystem::exists(cache_directory / "checkpoints") && !std::filesystem::is_empty(cache_directory / "checkpoints"))
                throw std::runtime_error("Local sync identity missing; restore its export outbox before grading");
            const auto producer = id();
            const auto created = clock();
            Json checkpoint = {{"schema_version", 1}, {"kind", "draxul.flashcard-checkpoint"}, {"checkpoint_id", id()},
                {"producer_id", producer}, {"created_at_unix", created}, {"created_at_utc", utc(created)},
                {"recall", Json::parse(serialize_state(recall()))}};
            return {{"schema_version", 1}, {"kind", "draxul.flashcard-outbox"}, {"producer_id", producer},
                {"checkpoint", checkpoint}, {"next_sequence", 1}, {"tentative", nullptr}, {"pending", Json::array()},
                {"delivered_count", 0}, {"latest", Json::object()}};
        }
        const auto box = document(*text);
        if (!box.is_object() || box.size() != 9 || number(box, "schema_version", 1) != 1
            || box.at("kind") != "draxul.flashcard-outbox" || !uuid(box.at("producer_id").get<std::string>())
            || !box.at("pending").is_array() || box["pending"].size() > max_pending
            || !box.at("latest").is_object() || box["latest"].size() > 512
            || number(box, "next_sequence", 1'000'000'001) == 0)
            throw std::runtime_error("Local review export outbox invalid; grading paused");
        (void)validate_checkpoint(box.at("checkpoint"));
        if (box["checkpoint"]["producer_id"] != box["producer_id"])
            throw std::runtime_error("Local bootstrap producer disagrees with export identity");
        delivered = static_cast<size_t>(number(box, "delivered_count", 1'000'000'000));
        if (delivered < box["latest"].size() || box["delivered_count"] >= box["next_sequence"])
            throw std::runtime_error("Inconsistent delivered review count");
        const auto producer = box["producer_id"].get<std::string>();
        const auto checkpoint_id = box["checkpoint"]["checkpoint_id"];
        std::set<std::string> ids;
        for (const auto& event : box["pending"])
        {
            validate_event(event, producer);
            if (event["base_checkpoint_id"] != checkpoint_id || !ids.insert(event["event_id"].get<std::string>()).second || event["producer_sequence"] >= box["next_sequence"])
                throw std::runtime_error("Duplicate or inconsistent local export intent");
        }
        for (const auto& [key, event] : box["latest"].items())
        {
            validate_event(event, producer);
            if (event["base_checkpoint_id"] != checkpoint_id || key != event_key(event) || event["producer_sequence"] >= box["next_sequence"])
                throw std::runtime_error("Invalid exported review summary key");
        }
        const auto& intent = box["tentative"];
        if (!intent.is_null())
        {
            if (!intent.is_object() || intent.size() != 3) throw std::runtime_error("Invalid prepared review export");
            validate_event(intent.at("event"), producer);
            const auto key = event_key(intent["event"]);
            const auto before = progress(intent.at("before"), key), after = progress(intent.at("after"), key);
            if (intent["event"]["base_checkpoint_id"] != checkpoint_id || after.reviews != before.reviews + 1 || after.reviews != intent["event"]["local_review_number"]
                || after.last_reviewed != intent["event"]["reviewed_at_unix"] || box["pending"].size() == max_pending
                || intent["event"]["producer_sequence"] >= box["next_sequence"] || after.assisted != before.assisted
                || (intent["event"]["outcome"] == "Again"
                    ? after.forgotten != before.forgotten + 1 || after.remembered != before.remembered
                    : after.remembered != before.remembered + 1 || after.forgotten != before.forgotten)
                || !ids.insert(intent["event"]["event_id"].get<std::string>()).second)
                throw std::runtime_error("Inconsistent prepared review export");
        }
        waiting = box["pending"].size() + (intent.is_null() ? 0 : 1);
        return box;
    }
    void save(const Json& box)
    {
        const auto text = box.dump();
        if (text.size() > max_document) throw std::runtime_error("Local review export outbox full; grading paused");
        save_outbox(text);
        waiting = box["pending"].size() + (box["tentative"].is_null() ? 0 : 1);
        delivered = box["delivered_count"].get<size_t>();
    }
    void resolve(Json& box, const State& state)
    {
        if (box["tentative"].is_null()) return;
        const auto intent = box["tentative"];
        const auto actual = record(state, event_key(intent["event"]));
        if (actual == intent["after"]) box["pending"].push_back(intent["event"]);
        else if (actual != intent["before"])
            throw std::runtime_error("Prepared review disagrees with local history; grading paused");
        box["tentative"] = nullptr;
        save(box); // A before match aborts an unsaved intent, never replays a grade.
    }
    void remember(const Json& value, bool checkpoint)
    {
        const auto id = value.at(checkpoint ? "checkpoint_id" : "event_id").get<std::string>();
        auto& values = checkpoint ? cached_checkpoints : cached_events;
        if (values.contains(id))
        {
            if (values.at(id) != value) throw std::runtime_error("Shared history ID conflict; cached schedule preserved");
            return;
        }
        if (values.size() >= (checkpoint ? 256 : 20'000))
            throw std::runtime_error("Shared history read limit reached; cached schedule preserved");
        const auto folder = cache_directory / (checkpoint ? "checkpoints" : "events");
        std::filesystem::create_directories(folder);
        write_file(folder / (id + ".json"), value, true);
        values.emplace(id, value);
    }
    bool load_cache()
    {
        const auto started = std::chrono::steady_clock::now();
        if (cache_loaded)
        {
            size_t added = 0;
            for (const bool checkpoint : { true, false })
            {
                const auto folder = cache_directory / (checkpoint ? "checkpoints" : "events");
                if (!std::filesystem::exists(folder)) continue;
                auto& values = checkpoint ? cached_checkpoints : cached_events;
                for (const auto& entry : std::filesystem::directory_iterator(folder))
                {
                    if (entry.path().extension() != ".json" || values.contains(entry.path().stem().string())) continue;
                    if (added >= 64 || std::chrono::steady_clock::now() - started > std::chrono::milliseconds(50))
                    { more_to_import = true; return false; }
                    const auto value = document(read_file(entry.path(), checkpoint ? max_document : 8192), checkpoint ? max_document : 8192);
                    if (checkpoint) (void)validate_checkpoint(value);
                    else validate_event(value, value.at("producer_id"));
                    if (entry.is_symlink() || value.at(checkpoint ? "checkpoint_id" : "event_id") != entry.path().stem().string())
                        throw std::runtime_error("Invalid local history cache identity; schedule preserved");
                    remember(value, checkpoint);
                    ++added;
                }
            }
            return true;
        }
        std::map<std::string, Json> checkpoints, events;
        for (const bool checkpoint : { true, false })
        {
            const auto folder = cache_directory / (checkpoint ? "checkpoints" : "events");
            if (!std::filesystem::exists(folder)) continue;
            auto& values = checkpoint ? checkpoints : events;
            for (const auto& entry : std::filesystem::directory_iterator(folder))
            {
                if (entry.path().extension() != ".json") continue; // Own incomplete staging files never become history.
                if (values.size() >= (checkpoint ? 256 : 20'000)
                    || std::chrono::steady_clock::now() - started > std::chrono::seconds(5))
                    throw std::runtime_error("Local history read budget reached; cached schedule preserved");
                const auto value = document(read_file(entry.path(), checkpoint ? max_document : 8192), checkpoint ? max_document : 8192);
                if (checkpoint) (void)validate_checkpoint(value);
                else validate_event(value, value.at("producer_id"));
                const auto id = value.at(checkpoint ? "checkpoint_id" : "event_id").get<std::string>();
                if (entry.path().stem() != std::filesystem::path(id) || entry.is_symlink())
                    throw std::runtime_error("Invalid local history cache identity; schedule preserved");
                values.emplace(id, value);
            }
        }
        cached_checkpoints = std::move(checkpoints); cached_events = std::move(events);
        cache_loaded = true;
        return true;
    }
    bool import_shared()
    {
        if (!std::filesystem::is_directory(directory / "flashcard-reviews")) return true;
        const auto started = std::chrono::steady_clock::now();
        size_t producers = 0, visited = 0, new_files = 0;
        std::map<std::string, int64_t> declared_counts;
        bool complete = true;
        for (const auto& producer_entry : std::filesystem::directory_iterator(directory / "flashcard-reviews"))
        {
            if (++producers > 128) throw std::runtime_error("Shared producer limit reached; cached schedule preserved");
            const auto producer = producer_entry.path().filename().string();
            if (producer_entry.is_symlink() || !producer_entry.is_directory() || !uuid(producer)) { complete = false; continue; }
            for (const auto& entry : std::filesystem::directory_iterator(producer_entry.path()))
            {
                if (++visited > 21'000) throw std::runtime_error("Shared file limit reached; cached schedule preserved");
                const auto name = entry.path().filename().string();
                if (name == "summary-v1.json")
                {
                    // A disposable snapshot only detects a positive sync gap;
                    // it never supplies grades or a schedule to the importer.
                    try
                    {
                        const auto summary = document(read_file(entry.path(), max_document));
                        validate_summary(summary, producer, clock());
                        if (entry.is_symlink()) { complete = false; continue; }
                        const auto declared = number(summary, "event_count", 1'000'000'000);
                        declared_counts[producer] = declared;
                    }
                    catch (const std::exception&) { complete = false; }
                    continue;
                }
                const auto token = producer + "/" + name;
                if (seen_shared.contains(token)) continue; // Valid source files are immutable.
                if (new_files >= 64 || std::chrono::steady_clock::now() - started > std::chrono::milliseconds(50))
                {
                    more_to_import = true;
                    return false;
                }
                const bool checkpoint = name.starts_with("checkpoint-");
                const auto id = checkpoint ? name.substr(11, name.size() > 16 ? name.size() - 16 : 0)
                                           : entry.path().stem().string();
                if (entry.is_symlink() || !entry.is_regular_file() || entry.path().extension() != ".json" || !uuid(id))
                { complete = false; continue; }
                try
                {
                    const auto value = document(read_file(entry.path(), checkpoint ? max_document : 8192), checkpoint ? max_document : 8192);
                    if (checkpoint) (void)validate_checkpoint(value);
                    else validate_event(value, producer);
                    if (value.at(checkpoint ? "checkpoint_id" : "event_id") != id || value.at("producer_id") != producer)
                        throw std::runtime_error("Shared file identity mismatch");
                    remember(value, checkpoint);
                    seen_shared.insert(token);
                    ++new_files;
                }
                catch (const std::exception&) { complete = false; }
            }
        }
        for (const auto& [producer, declared] : declared_counts)
        {
            const auto observed = std::count_if(cached_events.begin(), cached_events.end(), [&](const auto& event) {
                return event.second["producer_id"] == producer;
            });
            if (declared > observed) complete = false;
        }
        return complete;
    }
    bool reconstruct()
    {
        std::vector<std::string> checkpoints, events;
        for (const auto& [id, value] : cached_checkpoints) checkpoints.push_back(value.dump());
        for (const auto& [id, value] : cached_events) events.push_back(value.dump());
        history = rebuild_review_history(checkpoints, events, clock());
        if (history.unresolved) return false;
        const auto current = recall();
        if (current != history.state) save_recall(serialize_state(history.state));
        return true;
    }
};

ReviewExport::ReviewExport(std::filesystem::path directory, std::filesystem::path cache_directory, ReviewSession::Read read_outbox,
    ReviewSession::Save save_outbox, ReviewSession::Read read_recall, ReviewSession::Save save_recall,
    ReviewSession::Clock clock, Id id)
    : impl_(std::make_unique<Impl>(Impl{std::move(directory), std::move(cache_directory), std::move(read_outbox), std::move(read_recall),
          std::move(save_outbox), std::move(save_recall), std::move(clock), std::move(id)})) {}
ReviewExport::~ReviewExport() = default;
size_t ReviewExport::pending() const { return impl_->waiting; }
size_t ReviewExport::delivered() const { return impl_->delivered; }
bool ReviewExport::catching_up() const { return impl_->more_to_import; }
const std::string& ReviewExport::error() const { return impl_->failure; }

void ReviewExport::commit(std::string_view value)
{
    auto& i = *impl_;
    try
    {
        const auto before = i.recall(), after = parse_state(value);
        auto box = i.load();
        i.resolve(box, before);
        if (box["pending"].size() >= max_pending) throw std::runtime_error("512 reviews waiting to export; grading paused until delivery resumes");
        std::string changed;
        for (const auto& [key, p] : after)
            if (!before.contains(key) || before.at(key) != p)
            {
                if (!changed.empty()) throw std::runtime_error("Export requires exactly one explicit grade");
                changed = key;
            }
        if (changed.empty() || std::any_of(before.begin(), before.end(), [&](const auto& entry) { return !after.contains(entry.first); }))
            throw std::runtime_error("Export requires exactly one explicit grade");
        const auto prior = progress(record(before, changed), changed), graded = after.at(changed);
        const bool remembered = graded.remembered == prior.remembered + 1 && graded.forgotten == prior.forgotten;
        const bool again = graded.forgotten == prior.forgotten + 1 && graded.remembered == prior.remembered;
        if ((!remembered && !again) || graded.reviews != prior.reviews + 1 || graded.assisted != prior.assisted)
            throw std::runtime_error("Only explicit Again/Remembered can be exported");
        const auto colon = changed.find(':');
        Json event = {{"schema_version", 1}, {"kind", "draxul.flashcard-review"}, {"event_id", i.id()},
            {"base_checkpoint_id", box["checkpoint"]["checkpoint_id"]},
            {"producer_id", box["producer_id"]}, {"producer_sequence", box["next_sequence"]},
            {"word_id", changed.substr(colon + 1)}, {"direction", changed.substr(0, colon)},
            {"outcome", remembered ? "Remembered" : "Again"}, {"reviewed_at_unix", graded.last_reviewed},
            {"reviewed_at_utc", utc(graded.last_reviewed)}, {"local_review_number", graded.reviews}, {"explicit", true},
            {"source", {{"product", "dev.draxul.flashcards"}, {"version", "0.1.0"}, {"type", "self-report"}}}};
        validate_event(event, box["producer_id"]);
        box["next_sequence"] = box["next_sequence"].get<int64_t>() + 1;
        box["tentative"] = {{"event", event}, {"before", record(before, changed)}, {"after", record(after, changed)}};
        i.save(box); // Durable intent comes BEFORE the unchanged atomic recall save.
        try { i.save_recall(value); }
        catch (...)
        {
            // Some storage failures occur after replacement. Verify that boundary
            // before telling the caller to retry; a saved grade must not double.
            if (record(i.recall(), changed) != record(after, changed)) throw;
        }
        i.failure.clear();
    }
    catch (const std::exception& error) { i.failure = error.what(); throw; }
}

bool ReviewExport::flush()
{
    auto& i = *impl_;
    try
    {
        const bool initial = !i.read_outbox();
        auto box = i.load();
        if (initial) i.save(box); // Freeze legacy aggregate scores once, before any new event/import.
        i.resolve(box, i.recall());
        i.more_to_import = false;
        const bool local_complete = i.load_cache();
        i.remember(box["checkpoint"], true);
        for (const auto& [key, event] : box["latest"].items()) i.remember(event, false);
        for (const auto& event : box["pending"]) i.remember(event, false);
        const bool shared_complete = i.import_shared();
        const auto own_events = std::count_if(i.cached_events.begin(), i.cached_events.end(), [&](const auto& event) {
            return event.second["producer_id"] == box["producer_id"];
        });
        const bool complete = local_complete && shared_complete
            && own_events >= box["delivered_count"].get<int64_t>() + static_cast<int64_t>(box["pending"].size());
        const bool reconstructed = complete && i.reconstruct();
        if (!std::filesystem::is_directory(i.directory)) throw std::runtime_error("Shared learning folder unavailable; cached reviews and pending grades remain local");
        const auto directory = i.directory / "flashcard-reviews" / box["producer_id"].get<std::string>();
        std::filesystem::create_directories(directory);
        write_file(directory / ("checkpoint-" + box["checkpoint"]["checkpoint_id"].get<std::string>() + ".json"), box["checkpoint"], true);
        const size_t count = std::min<size_t>(32, box["pending"].size());
        for (size_t index = 0; index < count; ++index)
        {
            const auto& event = box["pending"][index];
            write_file(directory / (event["event_id"].get<std::string>() + ".json"), event, true);
            const auto key = event_key(event);
            if (!box["latest"].contains(key) || box["latest"][key]["producer_sequence"] < event["producer_sequence"])
                box["latest"][key] = event;
            box["delivered_count"] = box["delivered_count"].get<int64_t>() + 1;
        }
        box["pending"].erase(box["pending"].begin(), box["pending"].begin() + static_cast<Json::difference_type>(count));
        // Only this disposable latest cache is bounded. Immutable history and
        // undelivered intents are never pruned to make room for another grade.
        while (box["latest"].size() > 512)
        {
            auto oldest = box["latest"].begin();
            for (auto entry = box["latest"].begin(); entry != box["latest"].end(); ++entry)
                if (entry.value()["producer_sequence"] < oldest.value()["producer_sequence"]) oldest = entry;
            box["latest"].erase(oldest);
        }
        i.save(box); // If this fails, immutable event verification makes retry safe.
        const auto generated = i.clock();
        write_file(directory / "summary-v1.json", {{"schema_version", 1}, {"kind", "draxul.flashcard-summary"},
            {"producer_id", box["producer_id"]}, {"generated_at_unix", generated}, {"generated_at_utc", utc(generated)},
            {"event_count", box["delivered_count"]}, {"latest_limit", 512}, {"latest", box["latest"]},
            {"schedule", Json::parse(serialize_state(i.recall()))},
            {"sync", {{"events_seen", i.cached_events.size()}, {"checkpoints_seen", i.cached_checkpoints.size()},
                {"unresolved_events", i.history.unresolved}, {"read_complete", reconstructed},
                {"bootstrap_alternatives", i.history.alternative_bootstraps}}}}, false);
        i.failure = reconstructed ? (i.history.alternative_bootstraps
            ? "Alternative pre-sync score snapshots retained; largest review count selects each baseline." : "")
            : i.more_to_import ? "Catching up shared review history; cached schedule kept."
                              : "Shared history incomplete or conflicted; cached schedule kept until it can be verified.";
        return reconstructed;
    }
    catch (const std::exception& error) { i.failure = error.what(); return false; }
}
}
