#ifndef BROUK_CEF_TASK_MANAGER_HANDLER_H_
#define BROUK_CEF_TASK_MANAGER_HANDLER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.h"
#include "core/settings.h"
#include "include/cef_browser.h"
#include "include/cef_task_manager.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// A page Brouk shows, as the task manager lists it (020): a tab of any
// window and profile, or a window showing a Brouk page (help, settings…).
struct TaskManagerPage {
  // Stable while the page exists, e.g. "tab-12" or "window-settings".
  std::string key;
  // Null while it has no browser (not loaded yet, or unloaded).
  CefRefPtr<CefBrowser> browser;
  int pid = 0;  // Reported by its renderer; 0 when unknown.
  bool brouk_page = false;
  bool own_window = false;  // A window of its own, not a tab.
  std::string title;
  std::string url;
  std::string icon;  // A data: URL, or empty.
  std::string profile_name;
  std::string profile_color;  // One of ProfileColorNames().
  std::string window;         // The title of the window it is in.
  std::int64_t received_bytes = 0;
  bool audible = false;
  bool muted = false;
  bool camera = false;
  bool microphone = false;
  bool screen = false;
  bool paused = false;
  bool loading = false;
  bool crashed = false;
  bool devtools = false;
  // The tab shown in its window: it cannot be unloaded (as in Chrome).
  bool shown = false;
};

// What the task manager does to a page.
enum class TaskPageAction {
  kGoTo,
  kPause,
  kResume,
  kUnload,
  kMute,
  kUnmute,
  kReload,
  kDevTools,
};

// What the task manager page needs from the browser. UI thread.
class TaskManagerDelegate {
 public:
  virtual ~TaskManagerDelegate() = default;
  virtual std::vector<TaskManagerPage> Pages() = 0;
  // `key` is one of Pages(); unknown keys are ignored.
  virtual void Act(const std::string& key, TaskPageAction action) = 0;
  virtual const Settings& GlobalSettings() = 0;
  virtual std::optional<Error> SetGlobalSetting(std::string_view key,
                                                SettingValue value) = 0;
  virtual void CopyText(const std::string& text) = 0;
};

// Answers the task manager page's bridge messages (web/task-manager/api.ts,
// 020): its rows from Chromium's task manager and Brouk's pages, actions
// on pages and processes, and its settings. Only brouk://task-manager may
// ask. Measuring happens only when the page asks (once per refresh), so a
// closed task manager costs nothing. UI thread.
class TaskManagerHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit TaskManagerHandler(std::unique_ptr<TaskManagerDelegate> delegate);
  ~TaskManagerHandler() override;
  TaskManagerHandler(const TaskManagerHandler&) = delete;
  TaskManagerHandler& operator=(const TaskManagerHandler&) = delete;

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;

 private:
  struct HeapUsage {
    double used = -1;
    double total = -1;
  };
  struct NetworkSample {
    std::int64_t bytes = 0;
    double time = 0;  // Seconds.
  };

  std::string List(bool js_memory);
  // Asks every loaded page's JavaScript heap; answers arrive later and are
  // shown by the next List.
  void RequestHeapUsage(const std::vector<TaskManagerPage>& pages);

  std::unique_ptr<TaskManagerDelegate> delegate_;
  // Chromium measures only while this object exists (CEF adds an observer
  // for each one it hands out), so it is kept while the window is open.
  CefRefPtr<CefTaskManager> manager_;
  // Answers to RequestHeapUsage, by browser id; shared with the pending
  // requests, which may outlive this handler.
  std::shared_ptr<std::map<int, HeapUsage>> heap_;
  // The last count of each page, for bytes per second.
  std::map<std::string, NetworkSample> network_;
};

}  // namespace brouk

#endif  // BROUK_CEF_TASK_MANAGER_HANDLER_H_
