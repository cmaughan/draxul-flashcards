#pragma once

#include "review.h"
#include <filesystem>

namespace flashcards
{
std::string new_review_id();
struct SharedHistory
{
    State state;
    size_t events = 0, checkpoints = 0, unresolved = 0;
    bool alternative_bootstraps = false;
};
SharedHistory rebuild_review_history(const std::vector<std::string>& checkpoints,
    const std::vector<std::string>& events, int64_t now);

// Call commit/flush while holding the existing process-wide review lock.
// Recall keeps its current format. The independent atomic outbox owns export
// intent, so a shared-folder outage cannot erase or repeat a committed grade.
class ReviewExport
{
public:
    using Id = std::function<std::string()>;
    ReviewExport(std::filesystem::path directory, std::filesystem::path cache_directory, ReviewSession::Read read_outbox,
        ReviewSession::Save save_outbox, ReviewSession::Read read_recall,
        ReviewSession::Save save_recall, ReviewSession::Clock clock = unix_now,
        Id id = new_review_id);
    ~ReviewExport();
    void commit(std::string_view recall);
    bool flush();
    size_t pending() const;
    size_t delivered() const;
    bool catching_up() const;
    const std::string& error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
