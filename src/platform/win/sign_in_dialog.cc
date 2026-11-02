#include "platform/win/sign_in_dialog.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <optional>
#include <utility>

#include "platform/win/controls.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukSignIn";
constexpr int kWidth = 420;
constexpr int kMargin = 16;
constexpr int kTextHeight = 40;
constexpr int kRowHeight = 26;
constexpr int kRowGap = 10;
constexpr int kButtonHeight = 28;
constexpr int kButtonGap = 8;

enum ControlId : int {
  kUsernameId = 1,
  kPasswordId,
  kSignInId,
  kCancelId,
};

bool RegisterWindowClass() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

void Clear(std::wstring& text) {
  SecureZeroMemory(text.data(), text.size() * sizeof(wchar_t));
  text.clear();
}

}  // namespace

SignInDialog::SignInDialog(HWND owner,
                           const brouk::HttpAuthDialog& dialog,
                           HttpAuthDone done)
    : owner_(owner),
      username_(Utf8ToWide(dialog.username)),
      password_(Utf8ToWide(dialog.password)),
      theme_(CurrentTheme()),
      done_(std::move(done)) {
  message_ = Utf8ToWide(dialog.origin);
  // As Chrome: a login sent over plain http can be read on the way.
  if (dialog.origin.starts_with("http:")) {
    message_ += L"\r\nYour connection to this site is not private.";
  }
}

SignInDialog::~SignInDialog() {
  Clear(password_);
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  if (background_) {
    DeleteObject(background_);
  }
  if (field_) {
    DeleteObject(field_);
  }
  if (font_) {
    DeleteObject(font_);
  }
}

int SignInDialog::Scale(int dips) const {
  HWND reference = owner_ ? owner_ : GetDesktopWindow();
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(reference)), 96);
}

void SignInDialog::Show() {
  if (!RegisterWindowClass()) {
    Finish(std::nullopt);
    return;
  }
  const int rows = 2;
  const int width = Scale(kWidth);
  const int height = Scale(kMargin) * 2 + Scale(kTextHeight) + Scale(kRowGap) +
                     rows * (Scale(kRowHeight) + Scale(kRowGap)) +
                     Scale(kButtonHeight);
  RECT client = {};
  RECT frame = {0, 0, width, height};
  const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
  AdjustWindowRectEx(&frame, style, FALSE, WS_EX_DLGMODALFRAME);
  // In the middle of the owner, or of the screen without one.
  if (owner_ && IsWindow(owner_)) {
    GetWindowRect(owner_, &client);
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &client, 0);
  }
  const int frame_width = frame.right - frame.left;
  const int frame_height = frame.bottom - frame.top;
  const int x = (client.left + client.right - frame_width) / 2;
  const int y = (client.top + client.bottom - frame_height) / 2;
  const wchar_t* title = L"Sign in";
  window_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kWindowClass, title, style, x,
                            y, frame_width, frame_height, owner_, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    Finish(std::nullopt);
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(window_, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(&SignInDialog::WindowProc));
  background_ = CreateSolidBrush(theme_.toolbar);
  field_ = CreateSolidBrush(theme_.field);
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                             0, GetDpiForWindow(window_));
  font_ = CreateFontIndirectW(&metrics.lfMessageFont);
  CreateControls();
  // Modal to the owner, as a sheet on macOS.
  if (owner_ && IsWindow(owner_)) {
    owner_was_enabled_ = IsWindowEnabled(owner_) != FALSE;
    EnableWindow(owner_, FALSE);
  }
  ShowWindow(window_, SW_SHOW);
  // The password when the user name is known already.
  SetFocus(username_.empty() ? username_field_ : password_field_);
}

void SignInDialog::Cancel() {
  Finish(std::nullopt);
}

void SignInDialog::CreateControls() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  const int margin = Scale(kMargin);
  const int row = Scale(kRowHeight);
  const int gap = Scale(kRowGap);
  RECT client = {};
  GetClientRect(window_, &client);
  const int inner = client.right - 2 * margin;
  auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style,
                     int x, int y, int w, int h, int id) {
    HWND created = CreateWindowExW(
        0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(created, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    if (theme_.dark) {
      SetWindowTheme(created, L"DarkMode_Explorer", nullptr);
    }
    if (style & WS_TABSTOP) {
      SetWindowSubclass(created, &SignInDialog::ControlProc, 0,
                        reinterpret_cast<DWORD_PTR>(this));
    }
    return created;
  };
  int y = margin;
  control(WC_STATICW, message_.c_str(), SS_LEFT | SS_NOPREFIX, margin, y, inner,
          Scale(kTextHeight), 0);
  y += Scale(kTextHeight) + gap;
  username_field_ =
      control(WC_EDITW, username_.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, margin,
              y, inner, row, kUsernameId);
  SendMessageW(username_field_, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(L"Username"));
  SetAccessibleName(username_field_, L"Username");
  y += row + gap;
  password_field_ = control(WC_EDITW, password_.c_str(),
                            WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, margin,
                            y, inner, row, kPasswordId);
  SendMessageW(password_field_, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(L"Password"));
  SetAccessibleName(password_field_, L"Password");
  Clear(password_);  // Only the field keeps it now.
  y += row + gap;
  // Right to left: Sign In, Cancel.
  HDC dc = GetDC(window_);
  HGDIOBJ previous = SelectObject(dc, font_);
  auto width_of = [&](const wchar_t* text) {
    SIZE size = {};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    return std::max(static_cast<int>(size.cx) + Scale(24), Scale(84));
  };
  const int sign_in_width = width_of(L"Sign In");
  const int cancel_width = width_of(L"Cancel");
  SelectObject(dc, previous);
  ReleaseDC(window_, dc);
  const int button = Scale(kButtonHeight);
  int right = client.right - margin;
  control(WC_BUTTONW, L"Sign In", WS_TABSTOP | BS_DEFPUSHBUTTON,
          right - sign_in_width, y, sign_in_width, button, kSignInId);
  right -= sign_in_width + Scale(kButtonGap);
  control(WC_BUTTONW, L"Cancel", WS_TABSTOP | BS_PUSHBUTTON,
          right - cancel_width, y, cancel_width, button, kCancelId);
}

void SignInDialog::Submit() {
  std::wstring username = WindowText(username_field_);
  std::wstring password = WindowText(password_field_);
  HttpAuthAnswer answer{WideToUtf8(username), WideToUtf8(password)};
  Clear(password);
  Finish(std::move(answer));
}

void SignInDialog::Finish(std::optional<HttpAuthAnswer> answer) {
  if (finished_) {
    return;
  }
  finished_ = true;
  if (window_) {
    SetWindowTextW(password_field_, L"");
    // The owner first, so the activation goes back to it.
    if (owner_ && IsWindow(owner_) && owner_was_enabled_) {
      EnableWindow(owner_, TRUE);
    }
    ShowWindow(window_, SW_HIDE);
  }
  // May delete this dialog later (posted): nothing here touches it after.
  HttpAuthDone done = std::move(done_);
  done_ = nullptr;
  if (done) {
    done(std::move(answer));
  }
}

LRESULT CALLBACK SignInDialog::WindowProc(HWND window,
                                          UINT message,
                                          WPARAM wparam,
                                          LPARAM lparam) {
  auto* self =
      reinterpret_cast<SignInDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// Tab moves between the fields and buttons, Enter presses the focused
// button (in a field: the primary one), Escape cancels.
LRESULT CALLBACK SignInDialog::ControlProc(HWND window,
                                           UINT message,
                                           WPARAM wparam,
                                           LPARAM lparam,
                                           UINT_PTR /*id*/,
                                           DWORD_PTR data) {
  auto* self = reinterpret_cast<SignInDialog*>(data);
  switch (message) {
    case WM_GETDLGCODE:
      if (wparam == VK_TAB || wparam == VK_RETURN || wparam == VK_ESCAPE) {
        return DLGC_WANTALLKEYS;
      }
      break;
    case WM_KEYDOWN:
      if (wparam == VK_TAB) {
        if (HWND next =
                GetNextDlgTabItem(self->window_, window, KeyDown(VK_SHIFT))) {
          SetFocus(next);
        }
        return 0;
      }
      if (wparam == VK_RETURN) {
        if (GetDlgCtrlID(window) == kCancelId) {
          self->Finish(std::nullopt);
        } else {
          self->Submit();
        }
        return 0;
      }
      if (wparam == VK_ESCAPE) {
        self->Finish(std::nullopt);
        return 0;
      }
      break;
    case WM_CHAR:
      if (wparam == L'\t' || wparam == L'\r' || wparam == 0x1B) {
        return 0;  // Handled above; without this the edit beeps.
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &SignInDialog::ControlProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT SignInDialog::HandleMessage(UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam) {
  switch (message) {
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kSignInId:
          Submit();
          return 0;
        case kCancelId:
          Finish(std::nullopt);
          return 0;
        default:
          break;
      }
      break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      const bool field = message == WM_CTLCOLOREDIT;
      SetTextColor(dc, theme_.text);
      SetBkColor(dc, field ? theme_.field : theme_.toolbar);
      return reinterpret_cast<LRESULT>(field ? field_ : background_);
    }
    case WM_ERASEBKGND: {
      RECT client = {};
      GetClientRect(window_, &client);
      FillRect(reinterpret_cast<HDC>(wparam), &client, background_);
      return 1;
    }
    case WM_CLOSE:
      Finish(std::nullopt);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
