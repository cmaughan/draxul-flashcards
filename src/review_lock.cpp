#include "review_lock.h"
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace flashcards
{
namespace
{
class ReviewLock
{
public:
    explicit ReviewLock(const std::filesystem::path& directory)
    {
        if (directory.empty()) throw std::runtime_error("Storage path unavailable");
        std::filesystem::create_directories(directory);
        const auto file = directory / "recall-v1.lock";
#ifdef _WIN32
        handle_ = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("Review storage busy");
#else
        handle_ = open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (handle_ < 0) throw std::runtime_error("Review lock unavailable");
        if (flock(handle_, LOCK_EX | LOCK_NB) != 0)
        {
            close(handle_);
            handle_ = -1;
            throw std::runtime_error("Review storage busy");
        }
#endif
    }
    ~ReviewLock()
    {
#ifdef _WIN32
        CloseHandle(handle_);
#else
        flock(handle_, LOCK_UN);
        close(handle_);
#endif
    }
private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
};
}
std::shared_ptr<void> lock_review(const std::filesystem::path& directory)
{
    return std::make_shared<ReviewLock>(directory);
}
}
