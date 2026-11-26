#include "platform/win/choice_dialog.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <utility>

#include "platform/win/controls.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukChoice";
constexpr int kWidth = 440;
constexpr int kMargin = 16;
constexpr int kTextGap = 16;
constexpr int kButtonHeight = 28;
constexpr int kButtonGap = 8;
// Button ids are 1 + the button's index (0 is no control).
constexpr int kFirstButtonId = 1;

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

}  // namespace

ChoiceDialog::ChoiceDialog(HWND owner,
                           std::wstring title,
                           std::wstring message,
                           std::vector<std::wstring> buttons,
                           std::size_t cancel,
                           Done done)
    : owner_(owner),
      title_(std::move(title)),
      message_(std::move(message)),
      buttons_(std::move(buttons)),
      cancel_(cancel),
      theme_(CurrentTheme()),
      done_(std::move(done)) {}

ChoiceDialog::~ChoiceDialog() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  if (background_) {
    DeleteObject(background_);
  }
  if (font_) {
    DeleteObject(font_);
  }
}

int ChoiceDialog::Scale(int dips) const {
  HWND reference = owner_ ? owner_ : GetDesktopWindow();
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(reference)), 96);
}

void ChoiceDialog::Show() {
  if (buttons_.empty() || !RegisterWindowClass()) {
    Finish(cancel_);
    return;
  }
  const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
  window_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kWindowClass, title_.c_str(),
                            style, 0, 0, Scale(kWidth), Scale(kWidth), owner_,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    Finish(cancel_);
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(window_, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(&ChoiceDialog::WindowProc));
  background_ = CreateSolidBrush(theme_.toolbar);
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                             0, GetDpiForWindow(window_));
  font_ = CreateFontIndirectW(&metrics.lfMessageFont);

  // The message's height at this width, and the buttons' widths.
  const int margin = Scale(kMargin);
  const int inner = Scale(kWidth) - 2 * margin;
  HDC dc = GetDC(window_);
  HGDIOBJ previous = SelectObject(dc, font_);
  RECT text_rect = {0, 0, inner, 0};
  DrawTextW(dc, message_.c_str(), static_cast<int>(message_.size()), &text_rect,
            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
  std::vector<int> widths;
  for (const std::wstring& label : buttons_) {
    SIZE size = {};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()),
                          &size);
    widths.push_back(
        std::max(static_cast<int>(size.cx) + Scale(24), Scale(84)));
  }
  SelectObject(dc, previous);
  ReleaseDC(window_, dc);
  const int text_height = text_rect.bottom - text_rect.top;
  const int button = Scale(kButtonHeight);
  const int width = Scale(kWidth);
  const int height = margin + text_height + Scale(kTextGap) + button + margin;

  RECT frame = {0, 0, width, height};
  AdjustWindowRectEx(&frame, style, FALSE, WS_EX_DLGMODALFRAME);
  RECT area = {};
  // In the middle of the owner, or of the screen without one.
  if (owner_ && IsWindow(owner_)) {
    GetWindowRect(owner_, &area);
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
  }
  const int frame_width = frame.right - frame.left;
  const int frame_height = frame.bottom - frame.top;
  SetWindowPos(window_, nullptr, (area.left + area.right - frame_width) / 2,
               (area.top + area.bottom - frame_height) / 2, frame_width,
               frame_height, SWP_NOZORDER | SWP_NOACTIVATE);

  HINSTANCE instance = GetModuleHandleW(nullptr);
  auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style_bits,
                     int x, int y, int w, int h, int id) {
    HWND created = CreateWindowExW(
        0, type, text, WS_CHILD | WS_VISIBLE | style_bits, x, y, w, h, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(created, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    if (theme_.dark) {
      SetWindowTheme(created, L"DarkMode_Explorer", nullptr);
    }
    return created;
  };
  control(WC_STATICW, message_.c_str(), SS_LEFT | SS_NOPREFIX, margin, margin,
          inner, text_height, 0);
  // Right-aligned, in reading order.
  int total = 0;
  for (int w : widths) {
    total += w;
  }
  total += Scale(kButtonGap) * static_cast<int>(widths.size() - 1);
  int x = width - margin - total;
  const int y = margin + text_height + Scale(kTextGap);
  for (std::size_t i = 0; i < buttons_.size(); ++i) {
    HWND created =
        control(WC_BUTTONW, buttons_[i].c_str(),
                WS_TABSTOP | (i == 0 ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), x, y,
                widths[i], button, kFirstButtonId + static_cast<int>(i));
    SetWindowSubclass(created, &ChoiceDialog::ButtonProc, 0,
                      reinterpret_cast<DWORD_PTR>(this));
    if (i == 0) {
      first_button_ = created;
    }
    x += widths[i] + Scale(kButtonGap);
  }

  // Modal to the owner, as a sheet on macOS.
  if (owner_ && IsWindow(owner_)) {
    owner_was_enabled_ = IsWindowEnabled(owner_) != FALSE;
    EnableWindow(owner_, FALSE);
  }
  ShowWindow(window_, SW_SHOW);
  SetFocus(first_button_);
}

void ChoiceDialog::Finish(std::size_t choice) {
  if (finished_) {
    return;
  }
  finished_ = true;
  if (window_) {
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
    done(choice);
  }
}

LRESULT CALLBACK ChoiceDialog::WindowProc(HWND window,
                                          UINT message,
                                          WPARAM wparam,
                                          LPARAM lparam) {
  auto* self =
      reinterpret_cast<ChoiceDialog*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// Tab and the arrow keys move between the buttons, Enter presses the
// focused one, Escape cancels.
LRESULT CALLBACK ChoiceDialog::ButtonProc(HWND window,
                                          UINT message,
                                          WPARAM wparam,
                                          LPARAM lparam,
                                          UINT_PTR /*id*/,
                                          DWORD_PTR data) {
  auto* self = reinterpret_cast<ChoiceDialog*>(data);
  switch (message) {
    case WM_GETDLGCODE:
      if (wparam == VK_TAB || wparam == VK_RETURN || wparam == VK_ESCAPE ||
          wparam == VK_LEFT || wparam == VK_RIGHT) {
        return DLGC_WANTALLKEYS;
      }
      break;
    case WM_KEYDOWN:
      if (wparam == VK_TAB || wparam == VK_LEFT || wparam == VK_RIGHT) {
        const bool back =
            wparam == VK_LEFT || (wparam == VK_TAB && KeyDown(VK_SHIFT));
        if (HWND next = GetNextDlgTabItem(self->window_, window, back)) {
          SetFocus(next);
        }
        return 0;
      }
      if (wparam == VK_RETURN) {
        const int id = GetDlgCtrlID(window);
        self->Finish(static_cast<std::size_t>(id - kFirstButtonId));
        return 0;
      }
      if (wparam == VK_ESCAPE) {
        self->Finish(self->cancel_);
        return 0;
      }
      break;
    case WM_CHAR:
      if (wparam == L'\t' || wparam == L'\r' || wparam == 0x1B) {
        return 0;  // Handled above; without this the button beeps.
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &ChoiceDialog::ButtonProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT ChoiceDialog::HandleMessage(UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam) {
  switch (message) {
    case WM_COMMAND: {
      const int id = LOWORD(wparam);
      if (HIWORD(wparam) == BN_CLICKED && id >= kFirstButtonId &&
          id < kFirstButtonId + static_cast<int>(buttons_.size())) {
        Finish(static_cast<std::size_t>(id - kFirstButtonId));
        return 0;
      }
      break;
    }
    case WM_CTLCOLORSTATIC: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, theme_.text);
      SetBkColor(dc, theme_.toolbar);
      return reinterpret_cast<LRESULT>(background_);
    }
    case WM_ERASEBKGND: {
      RECT client = {};
      GetClientRect(window_, &client);
      FillRect(reinterpret_cast<HDC>(wparam), &client, background_);
      return 1;
    }
    case WM_CLOSE:
      Finish(cancel_);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
