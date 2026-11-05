#ifndef BROUK_PLATFORM_WIN_DEVTOOLS_DOCK_H_
#define BROUK_PLATFORM_WIN_DEVTOOLS_DOCK_H_

#include <windows.h>

#include <memory>
#include <optional>
#include <string>

#include "cef/devtools_channel.h"
#include "cef/devtools_frontend_client.h"
#include "cef/devtools_preferences.h"
#include "cef/devtools_tools_handler.h"
#include "cef/internal_page_client.h"
#include "core/request_rules.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#include "platform/win/theme.h"

namespace brouk::win {

// A tab's developer tools dock (006), as on macOS: a header with the dock's
// tools (Chromium's DevTools, then our own: Site data, Requests, Tokens,
// Rules) and buttons to move or close it, and the tool's browser below. The
// tab places the dock's window (at the bottom, left or right of the page, or
// in a window of its own). Without own tools (Brouk's pages) the dock is
// only DevTools. UI thread.
class DevToolsDock final : public DevToolsFrontendDelegate,
                           public InternalPageDelegate,
                           public ToolsHost {
 public:
  class Delegate {
   public:
    // The DevTools browser closed (the user closed the dock, or DevTools
    // went away): the tab's dev tools are off. The tab deletes the dock
    // later, not from inside this call.
    virtual void DockClosed(DevToolsDock& dock) = 0;
    // A position button: "bottom", "left", "right" or "window".
    virtual void DockWantsPosition(const std::string& position) = 0;
    // The divider was dragged to `size` (pixels); `done` when let go.
    virtual void DockResized(int size, bool done) = 0;
    // Keys DevTools did not use (shortcuts such as F12). True if handled.
    virtual bool DockKey(const CefKeyEvent& event) = 0;
    // "Open in new tab" in DevTools.
    virtual void DockOpenUrl(const std::string& url) = 0;
    // The tab profile's request rules (006), and changing them.
    virtual ToolsHost::Rules DockRequestRules() = 0;
    virtual std::optional<Error> DockSetRequestRules(RequestRuleSet rules) = 0;
    // A rule changed one of the tab's requests.
    virtual void DockRulesAffectedPage() = 0;
    // "Inspect" in our own tools' page: DevTools for that page.
    virtual void DockInspectTools(CefRefPtr<CefBrowser> tools,
                                  int x,
                                  int y) = 0;
    // Browsers opened (+1) or closed (-1): Brouk quits after all closed.
    virtual void DockBrowserCount(int delta) = 0;

   protected:
    ~Delegate() = default;
  };

  // Opens DevTools for `inspected` in a child window of `parent`; with
  // `inspect`, the element at that page point is selected in Elements. The
  // frontend runs in `context` (the internal pages' context) and keeps its
  // settings in `preferences` (the app's).
  DevToolsDock(HWND parent,
               CefRefPtr<CefBrowser> inspected,
               std::optional<POINT> inspect,
               CefRefPtr<CefRequestContext> context,
               DevToolsPreferences* preferences,
               bool own_tools,
               Delegate& delegate);
  ~DevToolsDock() override;
  DevToolsDock(const DevToolsDock&) = delete;
  DevToolsDock& operator=(const DevToolsDock&) = delete;

  HWND hwnd() const { return window_; }
  // Moves the dock into another window (the tab's, or its own).
  void SetParentWindow(HWND parent);
  // Where it is: highlights the position's button, and puts the divider on
  // the side facing the page ("window": none).
  void SetPosition(const std::string& position);
  void SetSecretHeaderNames(std::string names) {
    secret_headers_ = std::move(names);
  }
  // The security check's settings and the tab profile's folder (029).
  void SetSecurityCheck(SecurityCheckConfig config) {
    security_check_ = std::move(config);
  }
  // "Inspect" again while open: select the element at the page point.
  void InspectAt(POINT point);
  // A DevTools panel ("console"), or picking an element.
  void ShowPanel(const std::string& panel);
  void EnterInspectElementMode();
  // The Utilities tab at `utility` (027: menu, command palette, "Decode
  // with…"), with `text` as its input.
  void ShowUtility(const std::string& utility, std::optional<std::string> text);
  // The profile's request rules, applied to the tab while the dock is open.
  void ApplyRequestRules(const RequestRuleSet& rules);
  // Closes DevTools; the delegate hears DockClosed when done.
  void Close();
  // Whether both browsers are gone.
  bool closed() const;

  // DevToolsFrontendDelegate:
  void OnDevToolsCreated(CefRefPtr<CefBrowser> frontend) override;
  void OnDevToolsCloseReady() override;
  void OnDevToolsClosed() override;
  bool OnDevToolsKeyEvent(const CefKeyEvent& event,
                          CefEventHandle os_event) override;
  void OnDevToolsWantsClose() override;
  void OpenUrlFromDevTools(const std::string& url) override;
  void CopyTextFromDevTools(const std::string& text) override;

  // InternalPageDelegate (our own tools' page, brouk://tools):
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  void OnCloseReady() override;
  void OnBrowserClosed() override;
  void OnTitleChanged(const std::string& title) override;
  bool OnUnhandledKeyEvent(const CefKeyEvent& event,
                           CefEventHandle os_event) override;
  void OnInspectElement(int x, int y) override;

  // ToolsHost (our own tools' page):
  void SaveFiles(File main, std::optional<File> extra, Done done) override;
  std::string SecretHeaderNames() override { return secret_headers_; }
  SecurityCheckConfig SecurityCheck() override { return security_check_; }
  Rules RequestRules() override;
  std::optional<Error> SetRequestRules(RequestRuleSet rules) override;
  std::optional<UtilityInput> TakeUtilityInput() override;

 private:
  // A clickable part of the header.
  struct Part {
    RECT rect = {};
    std::string id;  // A tool ("devtools", "requests", …), a position, "close".
    std::wstring text;
    bool glyph = false;  // `text` is an icon font glyph.
  };

  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void Layout();
  void Paint();
  // The divider's strip at the edge facing the page, or empty.
  RECT DividerRect() const;
  void ShowTool(const std::string& tool);
  void FinishClosingIfDone();
  void UpdateFonts();
  int Scale(int dips) const;

  Delegate& delegate_;
  const bool own_tools_;
  CefRefPtr<CefRequestContext> context_;
  DevToolsPreferences* preferences_;  // Not owned; the app's.
  HWND window_ = nullptr;
  HWND content_ = nullptr;        // Holds the DevTools browser.
  HWND tools_content_ = nullptr;  // Holds our own tools' browser.
  Theme theme_;
  HFONT text_font_ = nullptr;
  HFONT icon_font_ = nullptr;
  std::string position_ = "bottom";
  std::string tool_ = "devtools";
  std::string secret_headers_;
  SecurityCheckConfig security_check_;
  std::optional<UtilityInput> utility_input_;
  std::vector<Part> parts_;
  // The divider being dragged: the mouse's start and the size then.
  bool dragging_ = false;
  POINT drag_start_ = {};
  int drag_size_ = 0;

  CefRefPtr<DevToolsChannel> channel_;
  CefRefPtr<DevToolsFrontendClient> client_;
  CefRefPtr<CefBrowser> frontend_;
  bool frontend_creating_ = true;
  CefRefPtr<InternalPageClient> tools_client_;
  CefRefPtr<CefBrowser> tools_;
  bool tools_creating_ = false;
  bool closing_ = false;
  bool closed_reported_ = false;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_DEVTOOLS_DOCK_H_
