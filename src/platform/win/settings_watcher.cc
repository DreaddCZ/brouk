#include "platform/win/settings_watcher.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

#include "cef/function_task.h"
#include "core/settings_file.h"
#include "include/base/cef_logging.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

struct SettingsWatcher::State {
  std::function<void()> changed;  // UI thread.
  bool alive = true;              // UI thread: false once the watcher is gone.
  std::atomic<bool> scheduled = false;  // Both threads: a reload is posted.
};

namespace {

constexpr int kDelayMs = 300;

// Closes a handle when it goes out of scope.
class Handle {
 public:
  explicit Handle(HANDLE handle) : handle_(handle) {}
  ~Handle() {
    if (valid()) {
      CloseHandle(handle_);
    }
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  HANDLE get() const { return handle_; }
  bool valid() const {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }

 private:
  HANDLE handle_;
};

// Whether the notifications in `buffer` name a settings file.
bool Relevant(const std::byte* buffer, DWORD size) {
  DWORD offset = 0;
  while (offset + sizeof(FILE_NOTIFY_INFORMATION) <= size) {
    FILE_NOTIFY_INFORMATION info;
    std::memcpy(&info, buffer + offset, sizeof(info));
    const std::size_t name_offset =
        offset + offsetof(FILE_NOTIFY_INFORMATION, FileName);
    if (name_offset + info.FileNameLength > size) {
      break;
    }
    std::wstring name(info.FileNameLength / sizeof(wchar_t), L'\0');
    std::memcpy(name.data(), buffer + name_offset, info.FileNameLength);
    if (IsLiveReloadedFile(WideToUtf8(name))) {
      return true;
    }
    if (info.NextEntryOffset == 0) {
      break;
    }
    offset += info.NextEntryOffset;
  }
  return false;
}

}  // namespace

void SettingsWatcher::Watch(const std::filesystem::path& directory,
                            HANDLE stop,
                            const std::shared_ptr<State>& state) {
  Handle folder(
      CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr));
  Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!folder.valid() || !event.valid()) {
    LOG(WARNING) << "Settings files are not watched (error " << GetLastError()
                 << ")";
    return;
  }
  alignas(DWORD) std::array<std::byte, 16 * 1024> buffer{};
  for (;;) {
    OVERLAPPED overlapped = {};
    overlapped.hEvent = event.get();
    ResetEvent(event.get());
    if (!ReadDirectoryChangesW(
            folder.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
            /*bWatchSubtree=*/TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_SIZE,
            nullptr, &overlapped, nullptr)) {
      LOG(WARNING) << "Settings files are no longer watched (error "
                   << GetLastError() << ")";
      return;
    }
    const std::array<HANDLE, 2> handles = {event.get(), stop};
    const DWORD woke = WaitForMultipleObjects(
        static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
    if (woke != WAIT_OBJECT_0) {
      // Stopped: cancel the read and wait for it, as it writes to buffer.
      CancelIoEx(folder.get(), &overlapped);
      DWORD ignored = 0;
      GetOverlappedResult(folder.get(), &overlapped, &ignored, TRUE);
      return;
    }
    DWORD size = 0;
    if (!GetOverlappedResult(folder.get(), &overlapped, &size, FALSE)) {
      continue;
    }
    // Size 0: too many changes for the buffer; reload to be safe.
    if (size == 0 || Relevant(buffer.data(), size)) {
      Schedule(state);
    }
  }
}

void SettingsWatcher::Schedule(const std::shared_ptr<State>& state) {
  if (state->scheduled.exchange(true)) {
    return;  // A reload is already coming.
  }
  CefPostDelayedTask(TID_UI, new FunctionTask([state] {
                       state->scheduled = false;
                       if (state->alive) {
                         state->changed();
                       }
                     }),
                     kDelayMs);
}

SettingsWatcher::SettingsWatcher(const std::filesystem::path& directory,
                                 std::function<void()> changed)
    : state_(std::make_shared<State>()),
      stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
  CEF_REQUIRE_UI_THREAD();
  state_->changed = std::move(changed);
  if (stop_) {
    // The thread gets its own copies of the path and the state.
    thread_ = std::thread([directory, stop = stop_, state = state_] {
      Watch(directory, stop, state);
    });
  }
}

SettingsWatcher::~SettingsWatcher() {
  CEF_REQUIRE_UI_THREAD();
  state_->alive = false;
  if (stop_) {
    SetEvent(stop_);
    if (thread_.joinable()) {
      thread_.join();
    }
    CloseHandle(stop_);
  }
}

}  // namespace brouk::win
