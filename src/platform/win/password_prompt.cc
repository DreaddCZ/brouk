#include "platform/win/password_prompt.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <string>
#include <string_view>
#include <utility>

#include "core/crypto.h"
#include "platform/win/controls.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukPasswordPrompt";
constexpr int kWidth = 360;
constexpr int kMargin = 16;
constexpr int kLabelWidth = 84;
constexpr int kRowHeight = 26;
constexpr int kRowGap = 10;
constexpr int kButtonWidth = 84;
constexpr int kButtonHeight = 28;
constexpr int kNeverWidth = 136;
// Glyphs of the "Segoe MDL2 Assets" icon font.
constexpr wchar_t kShowGlyph[] = L"";  // RedEye.
constexpr wchar_t kHideGlyph[] = L"";  // Hide.

enum ControlId : int {
  kUsernameId = 1,
  kPasswordId,
  kRevealId,
  kLocationId,
  kNeverId,
  kNotNowId,
  kSaveId,
};

bool RegisterWindowClass() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_DROPSHADOW;
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

// The text of an edit, wiped from the control's copy afterwards by the
// caller (SetWindowTextW) where it is a secret.
std::string FieldText(HWND field) {
  std::wstring wide = WindowText(field);
  std::string text = WideToUtf8(wide);
  SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
  return text;
}

}  // namespace

PasswordPromptBubble::PasswordPromptBubble(HWND owner,
                                           const PasswordPrompt& prompt,
                                           const Theme& theme,
                                           HFONT text_font,
                                           HFONT icon_font,
                                           PasswordPromptDone done)
    : owner_(owner),
      prompt_(prompt),
      theme_(theme),
      text_font_(text_font),
      icon_font_(icon_font),
      done_(std::move(done)) {}

PasswordPromptBubble::~PasswordPromptBubble() {
  if (window_) {
    if (password_) {
      SetWindowTextW(password_, L"");
    }
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  Wipe(prompt_.password);
  if (background_) {
    DeleteObject(background_);
  }
  if (field_) {
    DeleteObject(field_);
  }
}

int PasswordPromptBubble::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(owner_)), 96);
}

void PasswordPromptBubble::ShowBelow(const RECT& anchor) {
  if (!RegisterWindowClass()) {
    Finish(PasswordPromptAnswer::Choice::kNotNow);
    return;
  }
  // Title, site, user name, password, "Save in", buttons.
  const int rows = prompt_.locations.empty() ? 2 : 3;
  const int height = Scale(kMargin) * 2 + Scale(22) + Scale(20) + Scale(8) +
                     rows * (Scale(kRowHeight) + Scale(kRowGap)) +
                     Scale(kButtonHeight) + Scale(kRowGap);
  const int width = Scale(kWidth);
  const wchar_t* title =
      prompt_.update ? L"Update password?" : L"Save password?";
  window_ =
      CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, title,
                      WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
                      anchor.right - width, anchor.bottom, width, height,
                      owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    Finish(PasswordPromptAnswer::Choice::kNotNow);
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(
      window_, GWLP_WNDPROC,
      reinterpret_cast<LONG_PTR>(&PasswordPromptBubble::WindowProc));
  background_ = CreateSolidBrush(theme_.toolbar);
  field_ = CreateSolidBrush(theme_.field);
  CreateControls();
  // Shown without taking the keyboard, as Chrome's bubble: the page keeps
  // it while it loads (its navigation would take it back, which counted as
  // clicking elsewhere). A click in the prompt activates it.
  ShowWindow(window_, SW_SHOWNOACTIVATE);
}

void PasswordPromptBubble::CreateControls() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  const int margin = Scale(kMargin);
  const int row = Scale(kRowHeight);
  const int gap = Scale(kRowGap);
  const int label_width = Scale(kLabelWidth);
  RECT client = {};
  GetClientRect(window_, &client);
  const int field_x = margin + label_width;
  const int field_width = client.right - field_x - margin;
  auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style,
                     int x, int y, int w, int h, int id) {
    HWND created = CreateWindowExW(
        0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(created, WM_SETFONT, reinterpret_cast<WPARAM>(text_font_),
                 FALSE);
    if (theme_.dark) {
      SetWindowTheme(created,
                     type == std::wstring_view(WC_COMBOBOXW)
                         ? L"DarkMode_CFD"
                         : L"DarkMode_Explorer",
                     nullptr);
    }
    if (style & WS_TABSTOP) {
      SetWindowSubclass(created, &PasswordPromptBubble::ControlProc, 0,
                        reinterpret_cast<DWORD_PTR>(this));
    }
    return created;
  };
  int y = margin;
  control(WC_STATICW, prompt_.update ? L"Update password?" : L"Save password?",
          SS_LEFT | SS_NOPREFIX, margin, y, client.right - 2 * margin,
          Scale(22), 0);
  y += Scale(22);
  control(WC_STATICW, Utf8ToWide(prompt_.host).c_str(),
          SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, margin, y,
          client.right - 2 * margin, Scale(20), 0);
  y += Scale(20) + Scale(8);
  control(WC_STATICW, L"User name", SS_LEFT, margin, y + Scale(4), label_width,
          row, 0);
  username_ = control(WC_EDITW, Utf8ToWide(prompt_.username).c_str(),
                      WS_TABSTOP | ES_AUTOHSCROLL, field_x, y, field_width, row,
                      kUsernameId);
  SetAccessibleName(username_, L"User name");
  y += row + gap;
  control(WC_STATICW, L"Password", SS_LEFT, margin, y + Scale(4), label_width,
          row, 0);
  std::wstring password = Utf8ToWide(prompt_.password);
  password_ = control(WC_EDITW, password.c_str(),
                      WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, field_x, y,
                      field_width - row - Scale(4), row, kPasswordId);
  SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
  SetAccessibleName(password_, L"Password");
  password_char_ =
      static_cast<wchar_t>(SendMessageW(password_, EM_GETPASSWORDCHAR, 0, 0));
  reveal_ = control(WC_BUTTONW, kShowGlyph, WS_TABSTOP | BS_PUSHBUTTON,
                    client.right - margin - row, y, row, row, kRevealId);
  SendMessageW(reveal_, WM_SETFONT, reinterpret_cast<WPARAM>(icon_font_),
               FALSE);
  SetAccessibleName(reveal_, L"Show password");
  y += row + gap;
  if (!prompt_.locations.empty()) {
    control(WC_STATICW, L"Save in", SS_LEFT, margin, y + Scale(4), label_width,
            row, 0);
    location_ =
        control(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                field_x, y, field_width, Scale(300), kLocationId);
    for (const std::string& location : prompt_.locations) {
      SendMessageW(location_, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(Utf8ToWide(location).c_str()));
    }
    SendMessageW(location_, CB_SETCURSEL, 0, 0);
    SetAccessibleName(location_, L"Save in");
    y += row + gap;
  }
  const int button_width = Scale(kButtonWidth);
  const int button_height = Scale(kButtonHeight);
  save_ = control(WC_BUTTONW, prompt_.update ? L"Update" : L"Save",
                  WS_TABSTOP | BS_DEFPUSHBUTTON,
                  client.right - margin - button_width, y, button_width,
                  button_height, kSaveId);
  control(WC_BUTTONW, L"Not now", WS_TABSTOP | BS_PUSHBUTTON,
          client.right - margin - 2 * button_width - Scale(8), y, button_width,
          button_height, kNotNowId);
  if (!prompt_.update) {
    control(WC_BUTTONW, L"Never for this site", WS_TABSTOP | BS_PUSHBUTTON,
            margin, y, Scale(kNeverWidth), button_height, kNeverId);
  }
}

void PasswordPromptBubble::ToggleReveal() {
  revealed_ = !revealed_;
  SendMessageW(password_, EM_SETPASSWORDCHAR,
               revealed_ ? 0 : static_cast<WPARAM>(password_char_), 0);
  InvalidateRect(password_, nullptr, TRUE);
  SetWindowTextW(reveal_, revealed_ ? kHideGlyph : kShowGlyph);
  SetAccessibleName(reveal_, revealed_ ? L"Hide password" : L"Show password");
}

void PasswordPromptBubble::Finish(PasswordPromptAnswer::Choice choice) {
  if (finished_) {
    return;
  }
  finished_ = true;
  PasswordPromptAnswer answer;
  answer.choice = choice;
  if (window_) {
    answer.username = FieldText(username_);
    answer.password = FieldText(password_);
    SetWindowTextW(password_, L"");
    if (location_) {
      const LRESULT index = SendMessageW(location_, CB_GETCURSEL, 0, 0);
      answer.location = index == CB_ERR ? 0 : static_cast<int>(index);
    }
    ShowWindow(window_, SW_HIDE);
  }
  // May delete this prompt later (posted): nothing here touches it after.
  PasswordPromptDone done = std::move(done_);
  done_ = nullptr;
  if (done) {
    done(std::move(answer));
  }
}

LRESULT CALLBACK PasswordPromptBubble::WindowProc(HWND window,
                                                  UINT message,
                                                  WPARAM wparam,
                                                  LPARAM lparam) {
  auto* self = reinterpret_cast<PasswordPromptBubble*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// Tab moves between the fields and buttons, Enter saves (or presses the
// focused button), Escape is Not now.
LRESULT CALLBACK PasswordPromptBubble::ControlProc(HWND window,
                                                   UINT message,
                                                   WPARAM wparam,
                                                   LPARAM lparam,
                                                   UINT_PTR /*id*/,
                                                   DWORD_PTR data) {
  auto* self = reinterpret_cast<PasswordPromptBubble*>(data);
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
        if (SendMessageW(window, CB_GETDROPPEDSTATE, 0, 0)) {
          break;  // An open list takes Enter first.
        }
        const int id = GetDlgCtrlID(window);
        if (id == kRevealId) {
          self->ToggleReveal();
        } else {
          self->Finish(id == kNeverId    ? PasswordPromptAnswer::Choice::kNever
                       : id == kNotNowId ? PasswordPromptAnswer::Choice::kNotNow
                                         : PasswordPromptAnswer::Choice::kSave);
        }
        return 0;
      }
      if (wparam == VK_ESCAPE) {
        if (SendMessageW(window, CB_GETDROPPEDSTATE, 0, 0)) {
          break;
        }
        self->Finish(PasswordPromptAnswer::Choice::kNotNow);
        return 0;
      }
      break;
    case WM_CHAR:
      if (wparam == L'\t' || wparam == L'\r' || wparam == 0x1B) {
        return 0;  // Handled above; without this the edit beeps.
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &PasswordPromptBubble::ControlProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT PasswordPromptBubble::HandleMessage(UINT message,
                                            WPARAM wparam,
                                            LPARAM lparam) {
  switch (message) {
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kSaveId:
          Finish(PasswordPromptAnswer::Choice::kSave);
          return 0;
        case kNotNowId:
          Finish(PasswordPromptAnswer::Choice::kNotNow);
          return 0;
        case kNeverId:
          Finish(PasswordPromptAnswer::Choice::kNever);
          return 0;
        case kRevealId:
          ToggleReveal();
          return 0;
        default:
          break;
      }
      break;
    case WM_ACTIVATE:
      // Once the user has used the prompt, clicking elsewhere is "Not now";
      // the key icon offers it again.
      if (LOWORD(wparam) != WA_INACTIVE) {
        activated_ = true;
        SetFocus(save_);
        return 0;
      }
      if (activated_ && !finished_) {
        Finish(PasswordPromptAnswer::Choice::kNotNow);
      }
      break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      const bool field = message != WM_CTLCOLORSTATIC;
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
      Finish(PasswordPromptAnswer::Choice::kNotNow);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
