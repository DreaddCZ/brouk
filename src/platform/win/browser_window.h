#ifndef BROUK_PLATFORM_WIN_BROWSER_WINDOW_H_
#define BROUK_PLATFORM_WIN_BROWSER_WINDOW_H_

#include <windows.h>

#include <memory>
#include <string>

#include "cef/tab_client.h"
#include "include/cef_request_context.h"
#include "platform/win/tab.h"

namespace brouk::win {

class BrowserWindow;

// The app, as the browser windows see it. UI thread.
class BrowserWindowOwner {
 public:
  virtual CefRefPtr<CefRequestContext> RequestContext() = 0;
  virtual TabClientDelegate::SearchEngine SearchEngine() = 0;
  // Opens a new browser window with `url` (empty: a new, empty page).
  virtual void OpenWindow(const std::string& url) = 0;
  // The window is gone; the owner deletes it (later, not from inside its
  // window procedure).
  virtual void OnWindowDestroyed(BrowserWindow* window) = 0;

 protected:
  virtual ~BrowserWindowOwner() = default;
};

// A top-level browser window: toolbar (back, forward, reload, address bar)
// and one tab. W1 of 024 adds the tab strip in the title bar; until then a
// window holds a single tab, and links that would open a tab open a window.
class BrowserWindow final : public TabHost {
 public:
  explicit BrowserWindow(BrowserWindowOwner& owner);
  ~BrowserWindow() override;
  BrowserWindow(const BrowserWindow&) = delete;
  BrowserWindow& operator=(const BrowserWindow&) = delete;

  // Creates and shows the window and starts loading `url` (an empty page if
  // empty, with the address bar focused). False if Windows refused.
  bool Create(const std::string& url);
  void Navigate(const std::string& url);
  void Activate();

  // TabHost:
  void OnTabTitleChanged(const std::string& title) override;
  void OnTabAddressChanged(const std::string& url) override;
  void OnTabLoadingStateChanged(bool can_go_back, bool can_go_forward) override;
  void OnTabClosed() override;
  void OpenInNewWindow(const std::string& url) override;
  bool OnTabKey(const CefKeyEvent& event) override;
  TabClientDelegate::SearchEngine TabSearchEngine() override;

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  static LRESULT CALLBACK AddressProc(HWND window,
                                      UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam,
                                      UINT_PTR id,
                                      DWORD_PTR data);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

  void CreateControls();
  void UpdateFonts();
  void Layout();
  RECT ContentBounds() const;
  // Device pixels for `dips` at the window's current DPI.
  int Scale(int dips) const;
  // Browser shortcuts, from the page, the address bar or the window.
  bool HandleShortcut(int key, bool control, bool alt, bool shift);
  void FocusAddressBar();
  void NavigateToAddressBarText();
  void ShowAddress();

  BrowserWindowOwner& owner_;
  std::unique_ptr<Tab> tab_;
  HWND window_ = nullptr;
  HWND back_ = nullptr;
  HWND forward_ = nullptr;
  HWND reload_ = nullptr;
  HWND address_ = nullptr;
  HFONT text_font_ = nullptr;
  HFONT icon_font_ = nullptr;
  std::string url_;
  bool tab_closed_ = false;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_BROWSER_WINDOW_H_
