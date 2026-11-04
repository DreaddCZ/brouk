#ifndef BROUK_CORE_FILE_LOCK_H_
#define BROUK_CORE_FILE_LOCK_H_

#include <cstdint>
#include <filesystem>
#include <optional>

namespace brouk {

// An exclusive lock on a file, held until the object is destroyed or the
// process ends (also when it crashes). Tells a Brouk that only hands its
// links to the running one from that running one (025: Chromium's log).
// file_lock_posix.cc and file_lock_win.cc hold the two versions.
class FileLock {
 public:
  // Creates `path` if needed and locks it; nullopt if another process (or
  // another FileLock in this one) holds it, or it cannot be opened.
  static std::optional<FileLock> TryLock(const std::filesystem::path& path);

  FileLock(FileLock&& other) noexcept;
  FileLock& operator=(FileLock&& other) noexcept;
  FileLock(const FileLock&) = delete;
  FileLock& operator=(const FileLock&) = delete;
  ~FileLock();

 private:
  explicit FileLock(std::intptr_t handle) : handle_(handle) {}
  void Release();

  // The open file: a descriptor on POSIX, a HANDLE on Windows; -1 when
  // moved from.
  std::intptr_t handle_ = -1;
};

}  // namespace brouk

#endif  // BROUK_CORE_FILE_LOCK_H_
