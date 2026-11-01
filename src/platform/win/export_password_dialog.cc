#include "platform/win/export_password_dialog.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <utility>

#include "platform/win/controls.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukExportPassword";
constexpr int kWidth = 420;
constexpr int kMargin = 16;
constexpr int kTextHeight = 64;
constexpr int kRowHeight = 26;
constexpr int kRowGap = 10;
constexpr int kButtonHeight = 28;
constexpr int kButtonGap = 8;
// As on macOS: shorter export passwords are too easy to guess.
constexpr std::size_t kMinLength = 8;

enum ControlId : int {
  kFirstId = 1,
  kSecondId,
  kPrimaryId,
  kWithoutId,
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

ExportPasswordDialog::ExportPasswordDialog(HWND owner,
                                           bool confirm,
                                           std::wstring message,
                                           Done done)
    : owner_(owner),
      confirm_(confirm),
      message_(std::move(message)),
      theme_(CurrentTheme()),
      done_(std::move(done)) {}

ExportPasswordDialog::~ExportPasswordDialog() {
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

int ExportPasswordDialog::Scale(int dips) const {
  HWND reference = owner_ ? owner_ : GetDesktopWindow();
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(reference)), 96);
}

void ExportPasswordDialog::Show() {
  if (!RegisterWindowClass()) {
    Finish({});
    return;
  }
  const int rows = confirm_ ? 2 : 1;
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
  const wchar_t* title = confirm_
                             ? L"Protect the passwords with an export password"
                             : L"This profile contains passwords";
  window_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kWindowClass, title, style, x,
                            y, frame_width, frame_height, owner_, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    Finish({});
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(
      window_, GWLP_WNDPROC,
      reinterpret_cast<LONG_PTR>(&ExportPasswordDialog::WindowProc));
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
  SetFocus(first_);
}

void ExportPasswordDialog::CreateControls() {
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
      SetWindowSubclass(created, &ExportPasswordDialog::ControlProc, 0,
                        reinterpret_cast<DWORD_PTR>(this));
    }
    return created;
  };
  int y = margin;
  text_ = control(WC_STATICW, message_.c_str(), SS_LEFT | SS_NOPREFIX, margin,
                  y, inner, Scale(kTextHeight), 0);
  y += Scale(kTextHeight) + gap;
  first_ = control(WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                   margin, y, inner, row, kFirstId);
  SendMessageW(first_, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(L"Export password"));
  SetAccessibleName(first_, L"Export password");
  y += row + gap;
  if (confirm_) {
    second_ = control(WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                      margin, y, inner, row, kSecondId);
    SendMessageW(second_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Repeat the password"));
    SetAccessibleName(second_, L"Repeat the export password");
    y += row + gap;
  }
  // Right to left: the primary button, going on without, Cancel.
  const wchar_t* primary = confirm_ ? L"Export" : L"Import";
  const wchar_t* without =
      confirm_ ? L"Export without passwords" : L"Import without passwords";
  HDC dc = GetDC(window_);
  HGDIOBJ previous = SelectObject(dc, font_);
  auto width_of = [&](const wchar_t* text) {
    SIZE size = {};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    return std::max(static_cast<int>(size.cx) + Scale(24), Scale(84));
  };
  const int primary_width = width_of(primary);
  const int without_width = width_of(without);
  const int cancel_width = width_of(L"Cancel");
  SelectObject(dc, previous);
  ReleaseDC(window_, dc);
  const int button = Scale(kButtonHeight);
  int right = client.right - margin;
  control(WC_BUTTONW, primary, WS_TABSTOP | BS_DEFPUSHBUTTON,
          right - primary_width, y, primary_width, button, kPrimaryId);
  right -= primary_width + Scale(kButtonGap);
  control(WC_BUTTONW, without, WS_TABSTOP | BS_PUSHBUTTON,
          right - without_width, y, without_width, button, kWithoutId);
  right -= without_width + Scale(kButtonGap);
  control(WC_BUTTONW, L"Cancel", WS_TABSTOP | BS_PUSHBUTTON,
          right - cancel_width, y, cancel_width, button, kCancelId);
}

void ExportPasswordDialog::ShowProblem(const std::wstring& message) {
  if (!window_) {
    return;
  }
  SetWindowTextW(text_, message.c_str());
  SetWindowTextW(first_, L"");
  if (second_) {
    SetWindowTextW(second_, L"");
  }
  SetFocus(first_);
}

void ExportPasswordDialog::Submit() {
  std::wstring first = WindowText(first_);
  std::wstring second = second_ ? WindowText(second_) : std::wstring();
  if (confirm_ && first != second) {
    Clear(first);
    Clear(second);
    ShowProblem(L"The two passwords are not the same. Type them again.");
    return;
  }
  if (confirm_ && first.size() < kMinLength) {
    Clear(first);
    Clear(second);
    ShowProblem(
        L"Use at least 8 characters, so the file cannot be opened by "
        L"guessing.");
    return;
  }
  ExportPasswordAnswer answer;
  answer.choice = ExportPasswordAnswer::Choice::kPassword;
  answer.password = WideToUtf8(first);
  Clear(first);
  Clear(second);
  Finish(std::move(answer));
}

void ExportPasswordDialog::Finish(ExportPasswordAnswer answer) {
  if (finished_) {
    return;
  }
  finished_ = true;
  if (window_) {
    SetWindowTextW(first_, L"");
    if (second_) {
      SetWindowTextW(second_, L"");
    }
    // The owner first, so the activation goes back to it.
    if (owner_ && IsWindow(owner_) && owner_was_enabled_) {
      EnableWindow(owner_, TRUE);
    }
    ShowWindow(window_, SW_HIDE);
  }
  // May delete this dialog later (posted): nothing here touches it after.
  Done done = std::move(done_);
  done_ = nullptr;
  if (done) {
    done(std::move(answer));
  }
}

LRESULT CALLBACK ExportPasswordDialog::WindowProc(HWND window,
                                                  UINT message,
                                                  WPARAM wparam,
                                                  LPARAM lparam) {
  auto* self = reinterpret_cast<ExportPasswordDialog*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// Tab moves between the fields and buttons, Enter presses the focused
// button (in a field: the primary one), Escape cancels.
LRESULT CALLBACK ExportPasswordDialog::ControlProc(HWND window,
                                                   UINT message,
                                                   WPARAM wparam,
                                                   LPARAM lparam,
                                                   UINT_PTR /*id*/,
                                                   DWORD_PTR data) {
  auto* self = reinterpret_cast<ExportPasswordDialog*>(data);
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
        const int id = GetDlgCtrlID(window);
        if (id == kWithoutId) {
          self->Finish({ExportPasswordAnswer::Choice::kWithoutPasswords, {}});
        } else if (id == kCancelId) {
          self->Finish({});
        } else {
          self->Submit();
        }
        return 0;
      }
      if (wparam == VK_ESCAPE) {
        self->Finish({});
        return 0;
      }
      break;
    case WM_CHAR:
      if (wparam == L'\t' || wparam == L'\r' || wparam == 0x1B) {
        return 0;  // Handled above; without this the edit beeps.
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &ExportPasswordDialog::ControlProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT ExportPasswordDialog::HandleMessage(UINT message,
                                            WPARAM wparam,
                                            LPARAM lparam) {
  switch (message) {
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kPrimaryId:
          Submit();
          return 0;
        case kWithoutId:
          Finish({ExportPasswordAnswer::Choice::kWithoutPasswords, {}});
          return 0;
        case kCancelId:
          Finish({});
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
      Finish({});
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
