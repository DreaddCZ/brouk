#ifndef BROUK_PLATFORM_WIN_INTERNAL_PAGE_WINDOW_H_
#define BROUK_PLATFORM_WIN_INTERNAL_PAGE_WINDOW_H_

#include <windows.h>

#include <functional>
#include <memory>
#include <string>

#include "cef/internal_page_client.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"

namespace brouk::win {

// A window of its own for one internal page, such as help (016: help opens
// next to the page it explains, not as a tab). It has the system title
// bar; the page fills it. UI thread.
class InternalPageWindow final : public InternalPageDelegate {
 public:
  // `on_destroyed` is called once the window is gone; the owner deletes it
  // later (not from inside the call).
  explicit InternalPageWindow(std::function<void()> on_destroyed);
  ~InternalPageWindow() override;
  InternalPageWindow(const InternalPageWindow&) = delete;
  InternalPageWindow& operator=(const InternalPageWindow&) = delete;

  // Creates and shows the window at `width` x `height` (in 96-DPI pixels)
  // with `url`, answered by `handler`.
  bool Create(const std::string& url,
              const std::wstring& title,
              int width,
              int height,
              std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler,
              CefRefPtr<CefRequestContext> context);
  void Activate();
  HWND hwnd() const { return window_; }
  // Runs a script of ours in the page (e.g. help's showHelpSection).
  void ExecuteJavaScript(const std::string& code);

  // InternalPageDelegate:
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  void OnCloseReady() override;
  void OnBrowserClosed() override;
  void OnTitleChanged(const std::string& title) override;
  bool OnUnhandledKeyEvent(const CefKeyEvent& event,
                           CefEventHandle os_event) override;
  void OnInspectElement(int x, int y) override;

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  HWND BrowserWindowHandle() const;

  std::function<void()> on_destroyed_;
  CefRefPtr<InternalPageClient> client_;
  CefRefPtr<CefBrowser> browser_;
  HWND window_ = nullptr;
  bool browser_closed_ = false;
  bool close_requested_ = false;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_INTERNAL_PAGE_WINDOW_H_
