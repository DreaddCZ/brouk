#ifndef BROUK_PLATFORM_WIN_SETTINGS_WATCHER_H_
#define BROUK_PLATFORM_WIN_SETTINGS_WATCHER_H_

#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <thread>

namespace brouk::win {

// Watches the data folder for changes to settings files (and the request
// rules, 006, and link routing rules, 010): changes made in the files apply
// while Brouk runs (004). Chromium's own data under web\ is ignored.
//
// A thread waits for ReadDirectoryChangesW; it shares nothing with the UI
// thread but the State below, whose fields say which thread uses them.
// Create and delete on the UI thread.
class SettingsWatcher {
 public:
  // Calls `changed` on the UI thread, 0.3 s after a settings file changed
  // (editors write a file in several steps), at most once per change burst.
  SettingsWatcher(const std::filesystem::path& directory,
                  std::function<void()> changed);
  ~SettingsWatcher();
  SettingsWatcher(const SettingsWatcher&) = delete;
  SettingsWatcher& operator=(const SettingsWatcher&) = delete;

 private:
  struct State;
  // The thread: waits for changes until `stop` is set.
  static void Watch(const std::filesystem::path& directory,
                    HANDLE stop,
                    const std::shared_ptr<State>& state);
  // Posts the reload, unless one is already coming. Any thread.
  static void Schedule(const std::shared_ptr<State>& state);
  // Shared with the posted tasks, which may run after this is deleted.
  std::shared_ptr<State> state_;
  HANDLE stop_ = nullptr;  // Owned: an event that ends the thread.
  std::thread thread_;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_SETTINGS_WATCHER_H_
