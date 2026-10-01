#pragma once
#include <filesystem>
#include <memory>

namespace flashcards
{
// Nonblocking, process-wide file lock. Hold through the SDK read/atomic write.
// A second UI gets a retryable grade error instead of overwriting a schedule.
std::shared_ptr<void> lock_review(const std::filesystem::path& config_directory);
}
