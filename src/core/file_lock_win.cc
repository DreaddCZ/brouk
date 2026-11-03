// FileLock on Windows (file_lock_posix.cc is the macOS and Linux version).
#include "core/file_lock.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <utility>

namespace brouk {

namespace {

HANDLE AsHandle(std::intptr_t handle) {
  // NOLINTNEXTLINE(performance-no-int-to-ptr): the HANDLE kept as a number.
  return reinterpret_cast<HANDLE>(handle);
}

}  // namespace

std::optional<FileLock> FileLock::TryLock(const std::filesystem::path& path) {
  // Opened without sharing: while it is open, nobody else can open it,
  // this process included.
  const HANDLE file =
      CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }
  return FileLock(reinterpret_cast<std::intptr_t>(file));
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
  if (handle_ != -1) {
    CloseHandle(AsHandle(handle_));
    handle_ = -1;
  }
}

}  // namespace brouk
