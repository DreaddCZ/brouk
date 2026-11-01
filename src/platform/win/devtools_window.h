#ifndef BROUK_PLATFORM_WIN_DEVTOOLS_WINDOW_H_
#define BROUK_PLATFORM_WIN_DEVTOOLS_WINDOW_H_

#include <windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "platform/win/devtools_dock.h"
#include "platform/win/devtools_frame.h"

namespace brouk::win {

// Chromium DevTools for one of Brouk's own pages (settings, the profile
// manager, help, the dock's tools page, a brouk:// tab), in a window of its
// own (006: "internal HTML pages can be inspected"), as on macOS. Only
// DevTools: our own tools work on web tabs and their profiles. UI thread.
class DevToolsWindow final : public DevToolsDock::Delegate {
 public:
  // Callbacks into the app: shortcuts, opening a link, counting browsers
  // (Brouk quits after all closed), and that the window has closed (the
  // app deletes it later, not from inside).
  struct Hooks {
    std::function<bool(const CefKeyEvent& event)> key;
    std::function<void(const std::string& url)> open_url;
    std::function<void(int delta)> browser_count;
    std::function<void(DevToolsWindow& window)> closed;
  };

  DevToolsWindow(HWND beside,
                 CefRefPtr<CefBrowser> inspected,
                 const std::wstring& title,
                 std::optional<POINT> inspect,
                 CefRefPtr<CefRequestContext> context,
                 DevToolsPreferences* preferences,
                 Hooks hooks);
  ~DevToolsWindow();
  DevToolsWindow(const DevToolsWindow&) = delete;
  DevToolsWindow& operator=(const DevToolsWindow&) = delete;

  // The page it inspects (one window per page).
  int inspected_id() const { return inspected_id_; }
  void InspectAt(POINT point);
  void Activate();
  // Closes DevTools and the window.
  void Close();

  // DevToolsDock::Delegate:
  void DockClosed(DevToolsDock& dock) override;
  void DockWantsPosition(const std::string& /*position*/) override {}
  void DockResized(int /*size*/, bool /*done*/) override {}
  bool DockKey(const CefKeyEvent& event) override;
  void DockOpenUrl(const std::string& url) override;
  ToolsHost::Rules DockRequestRules() override { return {}; }
  std::optional<Error> DockSetRequestRules(RequestRuleSet /*rules*/) override {
    return Error{"Request rules are for web pages."};
  }
  void DockRulesAffectedPage() override {}
  void DockInspectTools(CefRefPtr<CefBrowser> /*tools*/,
                        int /*x*/,
                        int /*y*/) override {}
  void DockBrowserCount(int delta) override;

 private:
  Hooks hooks_;
  const int inspected_id_;
  std::unique_ptr<DevToolsFrame> frame_;
  std::unique_ptr<DevToolsDock> dock_;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_DEVTOOLS_WINDOW_H_
