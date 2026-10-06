#pragma once

#include <cstdint>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace flashcards
{
enum class Direction { Recognition, Production };
enum class VisualCue { Picture, ListenerReference, SubjectMarker, ApprovalReaction };
struct AudioClip
{
    std::string file, attribution, speaker;
    bool synthetic = false;
    bool private_cache = false;
};
struct Card
{
    std::string id, kana, romaji, image, attribution;
    std::vector<AudioClip> audio;
    Direction direction = Direction::Recognition;
    VisualCue cue = VisualCue::Picture;
    std::string cue_subject;
    std::string meaning;
};

struct QueuedCard
{
    const Card* card = nullptr;
    int64_t due = 0;
    bool due_now = false;
    bool current = false;
};

struct Progress
{
    int stage = 0;
    int64_t due = 0, last_reviewed = 0;
    int reviews = 0, remembered = 0, forgotten = 0, assisted = 0;
    bool operator==(const Progress&) const = default;
};

using State = std::map<std::string, Progress>;
std::vector<Card> parse_deck(std::string_view json);
std::map<std::string, std::vector<AudioClip>> parse_private_audio(std::string_view json, const std::vector<Card>& cards);
size_t next_audio_index(size_t current, size_t count);
State parse_state(std::string_view json);
std::string serialize_state(const State& state);
int64_t unix_now();

// Storage callbacks run on the SDK's main thread. Reads throw on IO errors;
// nullopt means a genuinely absent record. Saves replace a complete document
// atomically or throw, so no progress advances until the commit succeeds.
class ReviewSession
{
public:
    using Read = std::function<std::optional<std::string>()>;
    using Save = std::function<void(std::string_view)>;
    using Clock = std::function<int64_t()>;
    using Lock = std::function<std::shared_ptr<void>()>;
    ReviewSession(std::vector<Card> cards, Read read, Save save, Clock clock = unix_now, Lock lock = {});
    void refresh();
    void start_batch();
    bool flip();
    bool grade(bool remembered);
    void skip();
    const Card* current() const;
    bool revealed() const { return revealed_; }
    bool blocked() const { return blocked_; }
    const std::string& error() const { return error_; }
    size_t due_count() const;
    size_t deck_size() const { return cards_.size(); }
    std::optional<int64_t> next_due() const;
    // Read-only schedule: eligible relearning, reviewed, then new cards;
    // future cards follow by effective due time. Deck order breaks ties.
    // Includes the current card first; no recall state changes by inspecting it.
    std::vector<QueuedCard> queue() const;
    bool image_ready() const { return image_ready_; }
    bool batch_finished() const { return batch_grades_ >= 20; }
    size_t remaining_grades() const { return static_cast<size_t>(std::max(0, 20 - batch_grades_)); }
    void set_image_ready(bool ready) { image_ready_ = ready; }
private:
    void select();
    std::vector<size_t> ordered_indices(int64_t now) const;
    int64_t eligible_due(const Card& card, const State& state) const;
    std::string key(const Card& card) const;
    std::vector<Card> cards_;
    State state_;
    Read read_;
    Save save_;
    Clock clock_;
    Lock lock_;
    std::optional<size_t> current_;
    int expected_reviews_ = 0;
    int batch_grades_ = 0;
    bool revealed_ = false, blocked_ = false, image_ready_ = false;
    std::string error_;
    std::vector<std::string> skipped_;
};
} // namespace flashcards
