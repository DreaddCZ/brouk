// FileLock on macOS and Linux (file_lock_win.cc is the Windows version).
#include "core/file_lock.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <utility>

namespace brouk {

std::optional<FileLock> FileLock::TryLock(const std::filesystem::path& path) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): POSIX open.
  const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd < 0) {
    return std::nullopt;
  }
  // flock locks belong to the open file, so a second FileLock in this
  // process fails as well.
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    close(fd);
    return std::nullopt;
  }
  return FileLock(fd);
}

FileLock::FileLock(FileLock&& other) noexcept
    : handle_(std::exchange(other.handle_, -1)) {}

FileLock& FileLock::operator=(FileLock&& other) noexcept {
  if (this != &other) {
    Release();
    handle_ = std::exchange(other.handle_, -1);
  }
  return *this;
}

FileLock::~FileLock() {
  Release();
}

void FileLock::Release() {
  if (handle_ >= 0) {
    // Closing the file releases the lock.
    close(static_cast<int>(handle_));
    handle_ = -1;
  }
}

}  // namespace brouk
