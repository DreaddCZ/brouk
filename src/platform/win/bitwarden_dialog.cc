#include "platform/win/bitwarden_dialog.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <optional>
#include <string_view>
#include <utility>

#include "platform/win/controls.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukBitwardenPassword";
constexpr int kWidth = 380;
constexpr int kMargin = 16;
constexpr int kInfoHeight = 40;
constexpr int kProblemHeight = 34;
constexpr int kRowHeight = 26;
constexpr int kRowGap = 10;
constexpr int kButtonWidth = 84;
constexpr int kButtonHeight = 28;

enum ControlId : int {
  kFieldId = 1,
  kOkId,
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

// The host of a server address, or the address itself.
std::wstring HostOf(const std::string& url) {
  std::string_view rest = url;
  if (const std::size_t scheme = rest.find("://");
      scheme != std::string_view::npos) {
    rest.remove_prefix(scheme + 3);
  }
  rest = rest.substr(0, rest.find_first_of("/:?#"));
  return Utf8ToWide(rest.empty() ? std::string_view(url) : rest);
}

}  // namespace

BitwardenPasswordDialog::BitwardenPasswordDialog(
    HWND owner,
    const BitwardenPasswordRequest& request,
    BitwardenPasswordSubmit submit,
    std::function<void()> cancelled,
    std::function<void()> closed)
    : owner_(owner),
      request_(request),
      submit_(std::move(submit)),
      cancelled_(std::move(cancelled)),
      closed_(std::move(closed)),
      theme_(CurrentTheme()) {}

BitwardenPasswordDialog::~BitwardenPasswordDialog() {
  if (window_) {
    SetWindowTextW(field_, L"");
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  for (HGDIOBJ object :
       {static_cast<HGDIOBJ>(font_), static_cast<HGDIOBJ>(background_),
        static_cast<HGDIOBJ>(field_brush_)}) {
    if (object) {
      DeleteObject(object);
    }
  }
}

int BitwardenPasswordDialog::Scale(int dips) const {
  HWND reference = owner_ ? owner_ : GetDesktopWindow();
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(reference)), 96);
}

void BitwardenPasswordDialog::Show() {
  std::wstring title;
  std::wstring info;
  const wchar_t* ok = nullptr;
  if (request_.reprompt) {
    title = L"Confirm it's you";
    info =
        L"This login is protected in Bitwarden. Enter your master password "
        L"to fill it.";
    ok = L"Fill";
  } else {
    title = L"Unlock Bitwarden";
    const std::wstring server =
        request_.server.empty() ? L"" : L" on " + HostOf(request_.server);
    info = request_.account.empty()
               ? L"Enter your Bitwarden master password."
               : L"Enter the master password of " +
                     Utf8ToWide(request_.account) + server + L".";
    ok = L"Unlock";
  }
  if (!RegisterWindowClass()) {
    Cancel();
    return;
  }
  const int width = Scale(kWidth);
  const int height = Scale(kMargin) * 2 + Scale(kInfoHeight) + Scale(kRowGap) +
                     Scale(kRowHeight) + Scale(kRowGap) +
                     Scale(kProblemHeight) + Scale(kButtonHeight);
  const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
  RECT frame = {0, 0, width, height};
  AdjustWindowRectEx(&frame, style, FALSE, WS_EX_DLGMODALFRAME);
  RECT around = {};
  if (owner_ && IsWindow(owner_)) {
    GetWindowRect(owner_, &around);
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &around, 0);
  }
  const int frame_width = frame.right - frame.left;
  const int frame_height = frame.bottom - frame.top;
  window_ = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kWindowClass, title.c_str(), style,
      (around.left + around.right - frame_width) / 2,
      (around.top + around.bottom - frame_height) / 2, frame_width,
      frame_height, owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    Cancel();
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(
      window_, GWLP_WNDPROC,
      reinterpret_cast<LONG_PTR>(&BitwardenPasswordDialog::WindowProc));
  background_ = CreateSolidBrush(theme_.toolbar);
  field_brush_ = CreateSolidBrush(theme_.field);
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                             0, GetDpiForWindow(window_));
  font_ = CreateFontIndirectW(&metrics.lfMessageFont);
  CreateControls(info, ok);
  // Modal to the owner, as a sheet on macOS.
  if (owner_ && IsWindow(owner_)) {
    owner_was_enabled_ = IsWindowEnabled(owner_) != FALSE;
    EnableWindow(owner_, FALSE);
  }
  ShowWindow(window_, SW_SHOW);
  SetFocus(field_);
}

void BitwardenPasswordDialog::CreateControls(const std::wstring& info,
                                             const wchar_t* ok) {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  const int margin = Scale(kMargin);
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
      SetWindowSubclass(created, &BitwardenPasswordDialog::ControlProc, 0,
                        reinterpret_cast<DWORD_PTR>(this));
    }
    return created;
  };
  int y = margin;
  control(WC_STATICW, info.c_str(), SS_LEFT | SS_NOPREFIX, margin, y, inner,
          Scale(kInfoHeight), 0);
  y += Scale(kInfoHeight) + gap;
  field_ = control(WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
                   margin, y, inner, Scale(kRowHeight), kFieldId);
  SendMessageW(field_, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(L"Master password"));
  SetAccessibleName(field_, L"Master password");
  y += Scale(kRowHeight) + gap;
  problem_ = control(WC_STATICW, L"", SS_LEFT | SS_NOPREFIX, margin, y, inner,
                     Scale(kProblemHeight), 0);
  y += Scale(kProblemHeight);
  const int button_width = Scale(kButtonWidth);
  const int button_height = Scale(kButtonHeight);
  progress_ =
      control(WC_STATICW, request_.reprompt ? L"Checking…" : L"Unlocking…",
              SS_LEFT | SS_CENTERIMAGE, margin, y,
              inner - 2 * button_width - Scale(16), button_height, 0);
  ShowWindow(progress_, SW_HIDE);
  ok_ = control(WC_BUTTONW, ok, WS_TABSTOP | BS_DEFPUSHBUTTON,
                client.right - margin - button_width, y, button_width,
                button_height, kOkId);
  cancel_ = control(WC_BUTTONW, L"Cancel", WS_TABSTOP | BS_PUSHBUTTON,
                    client.right - margin - 2 * button_width - Scale(8), y,
                    button_width, button_height, kCancelId);
}

void BitwardenPasswordDialog::SetBusy(bool busy) {
  busy_ = busy;
  EnableWindow(field_, !busy);
  EnableWindow(ok_, !busy);
  EnableWindow(cancel_, !busy);
  ShowWindow(progress_, busy ? SW_SHOWNA : SW_HIDE);
  if (busy) {
    SetWindowTextW(problem_, L"");
  }
}

void BitwardenPasswordDialog::Submit() {
  if (busy_ || finished_ || !submit_) {
    return;
  }
  std::wstring wide = WindowText(field_);
  std::string password = WideToUtf8(wide);
  SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
  SetBusy(true);
  std::weak_ptr<int> alive = alive_;
  submit_(std::move(password), [this, alive](std::optional<std::string> error) {
    if (alive.expired() || finished_) {
      return;
    }
    if (error) {
      Failed(*error);
    } else {
      Close();
    }
  });
}

void BitwardenPasswordDialog::Failed(const std::string& message) {
  SetBusy(false);
  SetWindowTextW(problem_, Utf8ToWide(message).c_str());
  SetWindowTextW(field_, L"");
  SetFocus(field_);
}

void BitwardenPasswordDialog::Cancel() {
  if (finished_) {
    return;
  }
  std::function<void()> cancelled = std::move(cancelled_);
  cancelled_ = nullptr;
  Close();
  if (cancelled) {
    cancelled();
  }
}

void BitwardenPasswordDialog::Close() {
  if (finished_) {
    return;
  }
  finished_ = true;
  submit_ = nullptr;
  if (window_) {
    SetWindowTextW(field_, L"");
    // The owner first, so the activation goes back to it.
    if (owner_ && IsWindow(owner_) && owner_was_enabled_) {
      EnableWindow(owner_, TRUE);
    }
    ShowWindow(window_, SW_HIDE);
  }
  std::function<void()> closed = std::move(closed_);
  closed_ = nullptr;
  if (closed) {
    closed();
  }
}

LRESULT CALLBACK BitwardenPasswordDialog::WindowProc(HWND window,
                                                     UINT message,
                                                     WPARAM wparam,
                                                     LPARAM lparam) {
  auto* self = reinterpret_cast<BitwardenPasswordDialog*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// Tab moves between the field and buttons, Enter unlocks (or presses the
// focused button), Escape cancels.
LRESULT CALLBACK BitwardenPasswordDialog::ControlProc(HWND window,
                                                      UINT message,
                                                      WPARAM wparam,
                                                      LPARAM lparam,
                                                      UINT_PTR /*id*/,
                                                      DWORD_PTR data) {
  auto* self = reinterpret_cast<BitwardenPasswordDialog*>(data);
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
          self->Cancel();
        } else {
          self->Submit();
        }
        return 0;
      }
      if (wparam == VK_ESCAPE) {
        if (!self->busy_) {
          self->Cancel();
        }
        return 0;
      }
      break;
    case WM_CHAR:
      if (wparam == L'\t' || wparam == L'\r' || wparam == 0x1B) {
        return 0;  // Handled above; without this the edit beeps.
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &BitwardenPasswordDialog::ControlProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT BitwardenPasswordDialog::HandleMessage(UINT message,
                                               WPARAM wparam,
                                               LPARAM lparam) {
  switch (message) {
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kOkId:
          Submit();
          return 0;
        case kCancelId:
          Cancel();
          return 0;
        default:
          break;
      }
      break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      const bool field = message == WM_CTLCOLOREDIT;
      const bool problem = reinterpret_cast<HWND>(lparam) == problem_;
      // The failure in red, as on macOS.
      SetTextColor(dc, problem ? (theme_.dark ? RGB(0xFF, 0x7B, 0x72)
                                              : RGB(0xC4, 0x2B, 0x1C))
                               : theme_.text);
      SetBkColor(dc, field ? theme_.field : theme_.toolbar);
      return reinterpret_cast<LRESULT>(field ? field_brush_ : background_);
    }
    case WM_ERASEBKGND: {
      RECT client = {};
      GetClientRect(window_, &client);
      FillRect(reinterpret_cast<HDC>(wparam), &client, background_);
      return 1;
    }
    case WM_CLOSE:
      if (!busy_) {
        Cancel();
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
