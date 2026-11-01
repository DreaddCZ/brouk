#include "platform/win/autofill_prompt.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "platform/win/controls.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukAutofillPrompt";
constexpr int kWidth = 400;
constexpr int kMargin = 16;
constexpr int kNameWidth = 110;
constexpr int kLineHeight = 22;
constexpr int kRowHeight = 26;
constexpr int kGap = 10;
constexpr int kButtonHeight = 28;
constexpr int kButtonGap = 8;

enum ControlId : int {
  kLabelId = 1,
  kEditId,
  kNotNowId,
  kSaveNewId,
  kAcceptId,
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

// Orange, as the macOS prompt's failed validations.
COLORREF WarningColor(const Theme& theme) {
  return theme.dark ? RGB(0xFF, 0xB9, 0x4D) : RGB(0xB3, 0x5C, 0x00);
}

}  // namespace

AutofillPromptBubble::AutofillPromptBubble(HWND owner,
                                           const AutofillPrompt& prompt,
                                           const Theme& theme,
                                           HFONT font,
                                           AutofillPromptDone done)
    : owner_(owner),
      prompt_(prompt),
      theme_(theme),
      font_(font),
      done_(std::move(done)) {}

AutofillPromptBubble::~AutofillPromptBubble() {
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
}

int AutofillPromptBubble::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(owner_)), 96);
}

void AutofillPromptBubble::ShowBelow(const RECT& anchor) {
  if (!RegisterWindowClass()) {
    Finish(AutofillPromptAnswer::Choice::kNotNow);
    return;
  }
  const int width = Scale(kWidth);
  const int height = CreateControls(/*measure_only=*/true);
  window_ =
      CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Autofill",
                      WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
                      anchor.right - width, anchor.bottom, width, height,
                      owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    Finish(AutofillPromptAnswer::Choice::kNotNow);
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(
      window_, GWLP_WNDPROC,
      reinterpret_cast<LONG_PTR>(&AutofillPromptBubble::WindowProc));
  background_ = CreateSolidBrush(theme_.toolbar);
  field_ = CreateSolidBrush(theme_.field);
  CreateControls(/*measure_only=*/false);
  ShowWindow(window_, SW_SHOW);
  SetFocus(label_ && IsWindowVisible(label_) ? label_ : accept_);
}

// Lays the prompt out top to bottom; returns its height. With
// `measure_only`, creates nothing.
int AutofillPromptBubble::CreateControls(bool measure_only) {
  using Kind = AutofillPrompt::Kind;
  HINSTANCE instance = GetModuleHandleW(nullptr);
  const int margin = Scale(kMargin);
  const int width = Scale(kWidth) - 2;  // Inside the border.
  const int line = Scale(kLineHeight);
  auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style,
                     int x, int y, int w, int h, int id) -> HWND {
    if (measure_only) {
      return nullptr;
    }
    HWND created = CreateWindowExW(
        0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(created, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    if (theme_.dark) {
      SetWindowTheme(created, L"DarkMode_Explorer", nullptr);
    }
    if (style & WS_TABSTOP) {
      SetWindowSubclass(created, &AutofillPromptBubble::ControlProc, 0,
                        reinterpret_cast<DWORD_PTR>(this));
    }
    return created;
  };
  int y = margin;
  const std::wstring title =
      prompt_.kind == Kind::kAddTo
          ? L"Add to “" + Utf8ToWide(prompt_.entry_label) + L"”?"
      : prompt_.kind == Kind::kUpdate
          ? L"Update “" + Utf8ToWide(prompt_.entry_label) + L"”?"
          : L"Save as new entry?";
  control(WC_STATICW, title.c_str(), SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS,
          margin, y, width - 2 * margin, line, 0);
  y += line + Scale(6);
  const int name_width = Scale(kNameWidth);
  const int value_x = margin + name_width + Scale(8);
  const int value_width = width - value_x - margin;
  for (const AutofillPrompt::Line& row : prompt_.lines) {
    control(WC_STATICW, Utf8ToWide(row.field).c_str(),
            SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, margin, y, name_width, line,
            0);
    const std::wstring value =
        row.old_value.empty()
            ? Utf8ToWide(row.new_value)
            : Utf8ToWide(row.old_value) + L" → " + Utf8ToWide(row.new_value);
    control(WC_STATICW, value.c_str(), SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS,
            value_x, y, value_width, line, 0);
    y += line;
    if (!row.problem.empty()) {
      const std::wstring problem = L"⚠ " + Utf8ToWide(row.problem);
      if (HWND shown = control(WC_STATICW, problem.c_str(),
                               SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, value_x,
                               y, value_width, line, 0)) {
        problems_.push_back(shown);
      }
      y += line;
    }
  }
  y += Scale(6);
  // The label of a new entry; on an update "Save as new" keeps the
  // suggested one, as on macOS.
  if (prompt_.kind == Kind::kSaveNew) {
    control(WC_STATICW, L"Label", SS_LEFT, margin, y + Scale(4), name_width,
            Scale(kRowHeight), 0);
    label_ = control(WC_EDITW, Utf8ToWide(prompt_.suggested_label).c_str(),
                     WS_TABSTOP | ES_AUTOHSCROLL, value_x, y, value_width,
                     Scale(kRowHeight), kLabelId);
    if (label_) {
      SendMessageW(
          label_, EM_SETCUEBANNER, TRUE,
          reinterpret_cast<LPARAM>(L"e.g. Work, Home, Company invoicing"));
      SetAccessibleName(label_, L"Label of the new entry");
    }
    y += Scale(kRowHeight) + Scale(kGap);
  }
  const int button_height = Scale(kButtonHeight);
  if (!measure_only) {
    HDC dc = GetDC(window_);
    HGDIOBJ previous = SelectObject(dc, font_);
    auto width_of = [&](const wchar_t* text) {
      SIZE size = {};
      GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
      return std::max(static_cast<int>(size.cx) + Scale(24), Scale(72));
    };
    const wchar_t* accept = prompt_.kind == Kind::kAddTo    ? L"Add"
                            : prompt_.kind == Kind::kUpdate ? L"Update"
                                                            : L"Save";
    int right = width - margin;
    const int accept_width = width_of(accept);
    accept_ = control(WC_BUTTONW, accept, WS_TABSTOP | BS_DEFPUSHBUTTON,
                      right - accept_width, y, accept_width, button_height,
                      kAcceptId);
    right -= accept_width + Scale(kButtonGap);
    if (prompt_.kind == Kind::kUpdate) {
      const int save_new_width = width_of(L"Save as new");
      control(WC_BUTTONW, L"Save as new", WS_TABSTOP | BS_PUSHBUTTON,
              right - save_new_width, y, save_new_width, button_height,
              kSaveNewId);
      right -= save_new_width + Scale(kButtonGap);
    }
    const int not_now_width = width_of(L"Not now");
    control(WC_BUTTONW, L"Not now", WS_TABSTOP | BS_PUSHBUTTON,
            right - not_now_width, y, not_now_width, button_height, kNotNowId);
    control(WC_BUTTONW, L"Edit…", WS_TABSTOP | BS_PUSHBUTTON, margin, y,
            width_of(L"Edit…"), button_height, kEditId);
    SelectObject(dc, previous);
    ReleaseDC(window_, dc);
  }
  y += button_height + margin;
  return y + 2;  // With the border.
}

void AutofillPromptBubble::Finish(AutofillPromptAnswer::Choice choice) {
  if (finished_) {
    return;
  }
  finished_ = true;
  AutofillPromptAnswer answer;
  answer.choice = choice;
  answer.label =
      label_ ? WideToUtf8(WindowText(label_)) : prompt_.suggested_label;
  if (window_) {
    ShowWindow(window_, SW_HIDE);
  }
  // May delete this prompt later (posted): nothing here touches it after.
  AutofillPromptDone done = std::move(done_);
  done_ = nullptr;
  if (done) {
    done(std::move(answer));
  }
}

LRESULT CALLBACK AutofillPromptBubble::WindowProc(HWND window,
                                                  UINT message,
                                                  WPARAM wparam,
                                                  LPARAM lparam) {
  auto* self = reinterpret_cast<AutofillPromptBubble*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// Tab moves between the label and the buttons, Enter presses the focused
// button (in the label: the default one), Escape is Not now.
LRESULT CALLBACK AutofillPromptBubble::ControlProc(HWND window,
                                                   UINT message,
                                                   WPARAM wparam,
                                                   LPARAM lparam,
                                                   UINT_PTR /*id*/,
                                                   DWORD_PTR data) {
  auto* self = reinterpret_cast<AutofillPromptBubble*>(data);
  using Choice = AutofillPromptAnswer::Choice;
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
        self->Finish(id == kEditId      ? Choice::kEdit
                     : id == kNotNowId  ? Choice::kNotNow
                     : id == kSaveNewId ? Choice::kSaveAsNew
                                        : Choice::kAccept);
        return 0;
      }
      if (wparam == VK_ESCAPE) {
        self->Finish(Choice::kNotNow);
        return 0;
      }
      break;
    case WM_CHAR:
      if (wparam == L'\t' || wparam == L'\r' || wparam == 0x1B) {
        return 0;  // Handled above; without this the edit beeps.
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &AutofillPromptBubble::ControlProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT AutofillPromptBubble::HandleMessage(UINT message,
                                            WPARAM wparam,
                                            LPARAM lparam) {
  using Choice = AutofillPromptAnswer::Choice;
  switch (message) {
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kAcceptId:
          Finish(Choice::kAccept);
          return 0;
        case kSaveNewId:
          Finish(Choice::kSaveAsNew);
          return 0;
        case kNotNowId:
          Finish(Choice::kNotNow);
          return 0;
        case kEditId:
          Finish(Choice::kEdit);
          return 0;
        default:
          break;
      }
      break;
    case WM_ACTIVATE:
      // Clicking elsewhere is "Not now", as the macOS popover.
      if (LOWORD(wparam) == WA_INACTIVE && !finished_) {
        Finish(Choice::kNotNow);
      }
      break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      const bool field = message == WM_CTLCOLOREDIT;
      const bool problem =
          std::ranges::find(problems_, reinterpret_cast<HWND>(lparam)) !=
          problems_.end();
      SetTextColor(dc, problem ? WarningColor(theme_) : theme_.text);
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
      Finish(Choice::kNotNow);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
