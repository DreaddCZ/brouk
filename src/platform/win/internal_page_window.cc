#include "platform/win/internal_page_window.h"

#include <string_view>
#include <utility>

#include "include/wrapper/cef_helpers.h"
#include "platform/win/theme.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukInternalPageWindow";

bool RegisterWindowClass() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;  // Replaced per window.
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

}  // namespace

InternalPageWindow::InternalPageWindow(std::function<void()> on_destroyed)
    : on_destroyed_(std::move(on_destroyed)) {}

InternalPageWindow::~InternalPageWindow() {
  if (client_) {
    client_->DetachDelegate();
  }
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
}

bool InternalPageWindow::Create(
    const std::string& url,
    const std::wstring& title,
    int width,
    int height,
    std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler,
    CefRefPtr<CefRequestContext> context) {
  CEF_REQUIRE_UI_THREAD();
  if (!RegisterWindowClass()) {
    return false;
  }
  const int dpi = static_cast<int>(GetDpiForSystem());
  window_ = CreateWindowExW(
      0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(width, dpi, 96),
      MulDiv(height, dpi, 96), nullptr, nullptr, GetModuleHandleW(nullptr),
      nullptr);
  if (!window_) {
    return false;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(
      window_, GWLP_WNDPROC,
      reinterpret_cast<LONG_PTR>(&InternalPageWindow::WindowProc));
  ApplyThemeToFrame(window_, CurrentTheme());

  client_ = new InternalPageClient(this, std::move(handler));
  RECT bounds = {};
  GetClientRect(window_, &bounds);
  CefWindowInfo info;
  info.SetAsChild(window_, CefRect(0, 0, bounds.right, bounds.bottom));
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  CefBrowserHost::CreateBrowser(info, client_, url, settings, nullptr, context);
  ShowWindow(window_, SW_SHOWNORMAL);
  return true;
}

void InternalPageWindow::Activate() {
  if (IsIconic(window_)) {
    ShowWindow(window_, SW_RESTORE);
  }
  SetForegroundWindow(window_);
}

void InternalPageWindow::ExecuteJavaScript(const std::string& code) {
  if (browser_) {
    browser_->GetMainFrame()->ExecuteJavaScript(code, "", 0);
  }
}

HWND InternalPageWindow::BrowserWindowHandle() const {
  return browser_ ? browser_->GetHost()->GetWindowHandle() : nullptr;
}

void InternalPageWindow::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  if (close_requested_) {
    browser_->GetHost()->CloseBrowser(false);
    return;
  }
  RECT bounds = {};
  GetClientRect(window_, &bounds);
  SetWindowPos(BrowserWindowHandle(), nullptr, 0, 0, bounds.right,
               bounds.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
  browser_->GetHost()->SetFocus(true);
}

void InternalPageWindow::OnCloseReady() {
  if (HWND browser = BrowserWindowHandle()) {
    DestroyWindow(browser);
  }
}

void InternalPageWindow::OnBrowserClosed() {
  if (client_) {
    client_->DetachDelegate();
    client_ = nullptr;
  }
  browser_ = nullptr;
  browser_closed_ = true;
  PostMessageW(window_, WM_CLOSE, 0, 0);
}

void InternalPageWindow::OnTitleChanged(const std::string& /*title*/) {
  // The window keeps its own title ("Brouk Help").
}

bool InternalPageWindow::OnUnhandledKeyEvent(const CefKeyEvent& event,
                                             CefEventHandle /*os_event*/) {
  // Ctrl+W and Escape close the window, as other Windows tool windows.
  if (event.type == KEYEVENT_RAWKEYDOWN &&
      ((event.windows_key_code == 'W' &&
        (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0) ||
       event.windows_key_code == VK_ESCAPE)) {
    PostMessageW(window_, WM_CLOSE, 0, 0);
    return true;
  }
  return false;
}

void InternalPageWindow::OnInspectElement(int /*x*/, int /*y*/) {
  // Dev tools (006) come with W3.
}

// static
LRESULT CALLBACK InternalPageWindow::WindowProc(HWND window,
                                                UINT message,
                                                WPARAM wparam,
                                                LPARAM lparam) {
  auto* self = reinterpret_cast<InternalPageWindow*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT InternalPageWindow::HandleMessage(UINT message,
                                          WPARAM wparam,
                                          LPARAM lparam) {
  switch (message) {
    case WM_SIZE:
      if (HWND browser = BrowserWindowHandle()) {
        SetWindowPos(browser, nullptr, 0, 0, LOWORD(lparam), HIWORD(lparam),
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      return 0;
    case WM_SETFOCUS:
      if (browser_) {
        browser_->GetHost()->SetFocus(true);
      }
      return 0;
    case WM_DPICHANGED: {
      const auto* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SETTINGCHANGE:
      if (lparam && std::wstring_view(reinterpret_cast<const wchar_t*>(
                        lparam)) == L"ImmersiveColorSet") {
        ApplyThemeToFrame(window_, CurrentTheme());
      }
      break;
    case WM_CLOSE:
      // The page closes first (its unload handlers), then the window.
      if (browser_ && !browser_closed_) {
        browser_->GetHost()->CloseBrowser(false);
        return 0;
      }
      if (!browser_ && !browser_closed_ && client_) {
        close_requested_ = true;  // Closed as soon as the browser exists.
        return 0;
      }
      DestroyWindow(window_);
      return 0;
    case WM_DESTROY: {
      HWND window = window_;
      window_ = nullptr;
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      on_destroyed_();
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
