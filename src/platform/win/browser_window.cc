#include "platform/win/browser_window.h"

#include <commctrl.h>
#include <oleacc.h>

#include <algorithm>

#include "brouk/app_info.h"
#include "core/address_input.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukBrowserWindow";

// Toolbar layout in device-independent pixels (96 DPI).
constexpr int kToolbarHeight = 40;
constexpr int kButtonSize = 32;
constexpr int kMargin = 4;
constexpr int kAddressHeight = 28;

enum ControlId : int { kBackId = 1, kForwardId, kReloadId, kAddressId };

// Glyphs of the "Segoe MDL2 Assets" icon font, on Windows 10 and 11.
constexpr wchar_t kBackGlyph[] = L"";
constexpr wchar_t kForwardGlyph[] = L"";
constexpr wchar_t kReloadGlyph[] = L"";

bool RegisterWindowClass() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = DefWindowProcW;  // Replaced per window.
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

std::wstring WindowText(HWND window) {
  const int length = GetWindowTextLengthW(window);
  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(window, text.data(), length + 1);
  text.resize(static_cast<size_t>(length));
  return text;
}

// The name screen readers announce for a control whose text is an icon
// glyph (MSAA annotation, also read through UI Automation).
void SetAccessibleName(HWND control, const wchar_t* name) {
  IAccPropServices* services = nullptr;
  if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr,
                              CLSCTX_INPROC_SERVER, IID_IAccPropServices,
                              reinterpret_cast<void**>(&services)))) {
    return;
  }
  services->SetHwndPropStr(control, static_cast<DWORD>(OBJID_CLIENT),
                           CHILDID_SELF, PROPID_ACC_NAME, name);
  services->Release();
}

bool KeyDown(int key) {
  return (GetKeyState(key) & 0x8000) != 0;
}

}  // namespace

BrowserWindow::BrowserWindow(BrowserWindowOwner& owner) : owner_(owner) {}

BrowserWindow::~BrowserWindow() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  if (text_font_) {
    DeleteObject(text_font_);
  }
  if (icon_font_) {
    DeleteObject(icon_font_);
  }
}

bool BrowserWindow::Create(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!RegisterWindowClass()) {
    return false;
  }
  const UINT dpi = GetDpiForSystem();
  window_ =
      CreateWindowExW(0, kWindowClass, Utf8ToWide(kAppName).c_str(),
                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT,
                      CW_USEDEFAULT, MulDiv(1200, static_cast<int>(dpi), 96),
                      MulDiv(800, static_cast<int>(dpi), 96), nullptr, nullptr,
                      GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    return false;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(window_, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(&BrowserWindow::WindowProc));
  CreateControls();
  UpdateFonts();
  Layout();

  tab_ = std::make_unique<Tab>(*this);
  tab_->CreateBrowser(window_, ContentBounds(),
                      url.empty() ? std::string("about:blank") : url,
                      owner_.RequestContext());
  ShowWindow(window_, SW_SHOWNORMAL);
  if (url.empty()) {
    FocusAddressBar();
  }
  return true;
}

void BrowserWindow::CreateControls() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  auto button = [&](const wchar_t* glyph, ControlId id, const wchar_t* name) {
    HWND control = CreateWindowExW(
        0, WC_BUTTONW, glyph, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
        window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance,
        nullptr);
    SetAccessibleName(control, name);
    return control;
  };
  back_ = button(kBackGlyph, kBackId, L"Back");
  forward_ = button(kForwardGlyph, kForwardId, L"Forward");
  reload_ = button(kReloadGlyph, kReloadId, L"Reload");
  EnableWindow(back_, FALSE);
  EnableWindow(forward_, FALSE);
  address_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, WC_EDITW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddressId)), instance,
      nullptr);
  SendMessageW(address_, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(L"Search or type a web address"));
  SetWindowSubclass(address_, &BrowserWindow::AddressProc, 0,
                    reinterpret_cast<DWORD_PTR>(this));
}

int BrowserWindow::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(window_)), 96);
}

void BrowserWindow::UpdateFonts() {
  const UINT dpi = GetDpiForWindow(window_);
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                             0, dpi);
  HFONT text_font = CreateFontIndirectW(&metrics.lfMessageFont);
  LOGFONTW icon = {};
  icon.lfHeight = -MulDiv(16, static_cast<int>(dpi), 96);
  icon.lfWeight = FW_NORMAL;
  icon.lfCharSet = DEFAULT_CHARSET;
  icon.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(icon.lfFaceName, L"Segoe MDL2 Assets");
  HFONT icon_font = CreateFontIndirectW(&icon);
  for (HWND control : {back_, forward_, reload_}) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font),
                 TRUE);
  }
  SendMessageW(address_, WM_SETFONT, reinterpret_cast<WPARAM>(text_font), TRUE);
  if (text_font_) {
    DeleteObject(text_font_);
  }
  if (icon_font_) {
    DeleteObject(icon_font_);
  }
  text_font_ = text_font;
  icon_font_ = icon_font;
}

RECT BrowserWindow::ContentBounds() const {
  RECT client = {};
  GetClientRect(window_, &client);
  client.top = std::min(client.bottom, client.top + Scale(kToolbarHeight));
  return client;
}

void BrowserWindow::Layout() {
  RECT client = {};
  GetClientRect(window_, &client);
  const int button = Scale(kButtonSize);
  const int margin = Scale(kMargin);
  const int toolbar = Scale(kToolbarHeight);
  const int button_top = (toolbar - button) / 2;
  int x = margin;
  for (HWND control : {back_, forward_, reload_}) {
    SetWindowPos(control, nullptr, x, button_top, button, button,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    x += button + margin;
  }
  const int address_height = Scale(kAddressHeight);
  const int address_width =
      std::max(0, static_cast<int>(client.right) - x - margin);
  SetWindowPos(address_, nullptr, x, (toolbar - address_height) / 2,
               address_width, address_height, SWP_NOZORDER | SWP_NOACTIVATE);
  if (tab_) {
    tab_->SetBounds(ContentBounds());
  }
}

void BrowserWindow::Navigate(const std::string& url) {
  if (tab_) {
    tab_->Navigate(url);
    tab_->Focus();
  }
}

void BrowserWindow::Activate() {
  if (IsIconic(window_)) {
    ShowWindow(window_, SW_RESTORE);
  }
  SetForegroundWindow(window_);
}

void BrowserWindow::FocusAddressBar() {
  SetFocus(address_);
  SendMessageW(address_, EM_SETSEL, 0, -1);
}

void BrowserWindow::ShowAddress() {
  // A new, empty page shows an empty address bar (Chrome).
  const std::string shown = url_ == "about:blank" ? std::string() : url_;
  SetWindowTextW(address_, Utf8ToWide(shown).c_str());
}

void BrowserWindow::NavigateToAddressBarText() {
  const TabClientDelegate::SearchEngine engine = owner_.SearchEngine();
  const AddressInputResult input = ClassifyAddressInput(
      WideToUtf8(WindowText(address_)), engine.url_template);
  if (input.kind == AddressInputResult::Kind::kEmpty) {
    return;
  }
  Navigate(input.url);
}

bool BrowserWindow::HandleShortcut(int key,
                                   bool control,
                                   bool alt,
                                   bool shift) {
  // Chrome's Windows shortcuts for what W1 has so far.
  if ((control && !alt && key == 'L') || (alt && !control && key == 'D') ||
      (!control && !alt && key == VK_F6)) {
    FocusAddressBar();
    return true;
  }
  if ((!control && !alt && key == VK_F5) ||
      (control && !alt && !shift && key == 'R')) {
    if (tab_) {
      tab_->Reload();
    }
    return true;
  }
  if (alt && !control && key == VK_LEFT) {
    if (tab_) {
      tab_->GoBack();
    }
    return true;
  }
  if (alt && !control && key == VK_RIGHT) {
    if (tab_) {
      tab_->GoForward();
    }
    return true;
  }
  if (control && !alt && !shift && key == 'N') {
    owner_.OpenWindow(std::string());
    return true;
  }
  if (control && !alt && key == 'W') {  // Ctrl+W and Ctrl+Shift+W.
    PostMessageW(window_, WM_CLOSE, 0, 0);
    return true;
  }
  return false;
}

void BrowserWindow::OnTabTitleChanged(const std::string& title) {
  // "Page - Brouk", as Chrome's "Page - Google Chrome".
  std::wstring text = Utf8ToWide(kAppName);
  if (!title.empty()) {
    text = Utf8ToWide(title) + L" - " + text;
  }
  SetWindowTextW(window_, text.c_str());
}

void BrowserWindow::OnTabAddressChanged(const std::string& url) {
  url_ = url;
  if (GetFocus() != address_) {
    ShowAddress();
  }
}

void BrowserWindow::OnTabLoadingStateChanged(bool can_go_back,
                                             bool can_go_forward) {
  EnableWindow(back_, can_go_back);
  EnableWindow(forward_, can_go_forward);
}

void BrowserWindow::OnTabClosed() {
  tab_closed_ = true;
  // Closing the window from a task, not inside CEF's close callback.
  PostMessageW(window_, WM_CLOSE, 0, 0);
}

void BrowserWindow::OpenInNewWindow(const std::string& url) {
  owner_.OpenWindow(url);
}

bool BrowserWindow::OnTabKey(const CefKeyEvent& event) {
  if (event.type != KEYEVENT_RAWKEYDOWN) {
    return false;
  }
  return HandleShortcut(event.windows_key_code,
                        (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0,
                        (event.modifiers & EVENTFLAG_ALT_DOWN) != 0,
                        (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0);
}

TabClientDelegate::SearchEngine BrowserWindow::TabSearchEngine() {
  return owner_.SearchEngine();
}

// static
LRESULT CALLBACK BrowserWindow::WindowProc(HWND window,
                                           UINT message,
                                           WPARAM wparam,
                                           LPARAM lparam) {
  auto* self = reinterpret_cast<BrowserWindow*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// static
LRESULT CALLBACK BrowserWindow::AddressProc(HWND window,
                                            UINT message,
                                            WPARAM wparam,
                                            LPARAM lparam,
                                            UINT_PTR /*id*/,
                                            DWORD_PTR data) {
  auto* self = reinterpret_cast<BrowserWindow*>(data);
  switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      const int key = static_cast<int>(wparam);
      if (key == VK_RETURN) {
        self->NavigateToAddressBarText();
        return 0;
      }
      if (key == VK_ESCAPE) {
        self->ShowAddress();
        SendMessageW(window, EM_SETSEL, 0, -1);
        return 0;
      }
      if (self->HandleShortcut(key, KeyDown(VK_CONTROL), KeyDown(VK_MENU),
                               KeyDown(VK_SHIFT))) {
        return 0;
      }
      break;
    }
    case WM_CHAR:
      // Enter and Escape are handled above; without this the edit beeps.
      if (wparam == L'\r' || wparam == 0x1B) {
        return 0;
      }
      break;
    case WM_KILLFOCUS:
      self->ShowAddress();
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &BrowserWindow::AddressProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT BrowserWindow::HandleMessage(UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam) {
  switch (message) {
    case WM_SIZE:
      Layout();
      return 0;
    case WM_DPICHANGED: {
      const auto* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      UpdateFonts();
      Layout();
      return 0;
    }
    case WM_COMMAND:
      if (tab_) {
        switch (LOWORD(wparam)) {
          case kBackId:
            tab_->GoBack();
            return 0;
          case kForwardId:
            tab_->GoForward();
            return 0;
          case kReloadId:
            tab_->Reload();
            return 0;
          default:
            break;
        }
      }
      break;
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize.x = Scale(400);
      info->ptMinTrackSize.y = Scale(300);
      return 0;
    }
    case WM_CLOSE:
      // The page decides first (unload handlers, "leave site?"); the window
      // closes once its tab has (OnTabClosed).
      if (tab_ && !tab_closed_) {
        tab_->Close();
        return 0;
      }
      DestroyWindow(window_);
      return 0;
    case WM_DESTROY: {
      HWND window = window_;
      window_ = nullptr;
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      owner_.OnWindowDestroyed(this);
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
