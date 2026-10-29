#include "platform/win/tab_panel.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <utility>

#include "cef/function_task.h"
#include "include/cef_task.h"
#include "platform/win/controls.h"

namespace brouk::win {

namespace {

constexpr wchar_t kPanelClass[] = L"BroukTabPanel";
constexpr wchar_t kCloseGlyph[] = L"\uE711";

// Layout in device-independent pixels (96 DPI).
constexpr int kPadding = 20;
constexpr int kTopOffset = 16;  // Dialogs from the top of the page.
constexpr int kPromptLeft = 8;  // The permission prompt from the left.
constexpr int kPromptTop = 6;
constexpr int kMaxMessageHeight = 300;
constexpr int kCornerRadius = 8;

enum ControlId : int {
  kTitleId = 1,
  kCloseId,
  kMessageId,
  kDetailId,
  kInputId,
  kCheckboxId,
  kCheckboxLabelId,
  kFirstLineId = 100,
  kFirstButtonId = 200,
};

bool RegisterPanelClass(WNDPROC procedure) {
  static const bool registered = [procedure] {
    WNDCLASSEXW panel_class = {};
    panel_class.cbSize = sizeof(panel_class);
    panel_class.lpfnWndProc = procedure;
    panel_class.hInstance = GetModuleHandleW(nullptr);
    panel_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    panel_class.lpszClassName = kPanelClass;
    return RegisterClassExW(&panel_class) != 0;
  }();
  return registered;
}

// The height of `text` wrapped to `width` pixels.
int TextHeight(HDC dc, HFONT font, const std::wstring& text, int width) {
  RECT bounds = {0, 0, width, 0};
  HGDIOBJ previous = SelectObject(dc, font);
  DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds,
            DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
  SelectObject(dc, previous);
  return bounds.bottom;
}

int TextWidth(HDC dc, HFONT font, const std::wstring& text) {
  RECT bounds = {};
  HGDIOBJ previous = SelectObject(dc, font);
  DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds,
            DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, previous);
  return bounds.right;
}

// A multi-line edit wants "\r\n" between lines; pages send "\n".
std::wstring EditLineBreaks(const std::wstring& text) {
  std::wstring result;
  result.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    const wchar_t c = text[i];
    if (c == L'\r' || c == L'\n') {
      result += L"\r\n";
      if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') {
        ++i;
      }
    } else {
      result.push_back(c);
    }
  }
  return result;
}

HFONT GlyphFont(UINT dpi, int dips) {
  LOGFONTW glyph = {};
  glyph.lfHeight = -MulDiv(dips, static_cast<int>(dpi), 96);
  glyph.lfWeight = FW_NORMAL;
  glyph.lfCharSet = DEFAULT_CHARSET;
  glyph.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(glyph.lfFaceName, L"Segoe MDL2 Assets");
  return CreateFontIndirectW(&glyph);
}

}  // namespace

TabPanel::TabPanel(HWND parent,
                   const Theme& theme,
                   Content content,
                   Done done,
                   Shortcuts shortcuts)
    : parent_(parent),
      theme_(theme),
      content_(std::move(content)),
      done_(std::move(done)),
      shortcuts_(std::move(shortcuts)),
      dpi_(GetDpiForWindow(parent)) {
  background_brush_ = CreateSolidBrush(theme_.toolbar);
  field_brush_ = CreateSolidBrush(theme_.field);
  CreateFonts();
  if (!RegisterPanelClass(&TabPanel::PanelProc)) {
    return;
  }
  window_ =
      CreateWindowExW(0, kPanelClass, content_.title.c_str(),
                      WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                      parent_, nullptr, GetModuleHandleW(nullptr), this);
  if (!window_) {
    return;
  }
  CreateControls();
  if (content_.placement != Placement::kFill) {
    const int width = Scale(content_.width);
    height_ = Layout(Scale(kPadding), Scale(kPadding),
                     width - 2 * Scale(kPadding), true) +
              2 * Scale(kPadding);
    const int radius = Scale(kCornerRadius);
    // The system owns the region from here on.
    SetWindowRgn(
        window_,
        CreateRoundRectRgn(0, 0, width + 1, height_ + 1, radius, radius),
        FALSE);
  }
  Place();
  ShowWindow(window_, SW_SHOWNA);
}

TabPanel::~TabPanel() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  for (HFONT font : {text_font_, title_font_, small_font_, glyph_font_,
                     small_glyph_font_, icon_font_}) {
    if (font) {
      DeleteObject(font);
    }
  }
  for (HBRUSH brush : {background_brush_, field_brush_}) {
    if (brush) {
      DeleteObject(brush);
    }
  }
}

int TabPanel::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(dpi_), 96);
}

void TabPanel::CreateFonts() {
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                             0, dpi_);
  LOGFONTW text = metrics.lfMessageFont;
  text_font_ = CreateFontIndirectW(&text);
  LOGFONTW title = text;
  title.lfWeight = FW_SEMIBOLD;
  if (content_.placement == Placement::kFill) {
    title.lfHeight = -Scale(22);
  }
  title_font_ = CreateFontIndirectW(&title);
  LOGFONTW small = text;
  small.lfHeight = -Scale(11);
  small_font_ = CreateFontIndirectW(&small);
  glyph_font_ = GlyphFont(dpi_, 16);
  small_glyph_font_ = GlyphFont(dpi_, 10);
  icon_font_ = GlyphFont(dpi_, 44);
}

HWND TabPanel::AddControl(const wchar_t* control_class,
                          const std::wstring& text,
                          DWORD style,
                          int id,
                          HFONT font) {
  HWND control = CreateWindowExW(
      0, control_class, text.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
      window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      GetModuleHandleW(nullptr), nullptr);
  if (control) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
  }
  return control;
}

void TabPanel::CreateControls() {
  constexpr DWORD kLabel = SS_NOPREFIX | SS_EDITCONTROL;
  if (!content_.title.empty()) {
    title_ =
        AddControl(L"STATIC", content_.title, kLabel, kTitleId, title_font_);
  }
  if (content_.close_button) {
    close_ = AddControl(L"BUTTON", L"Close", BS_OWNERDRAW | WS_TABSTOP,
                        kCloseId, text_font_);
  }
  for (size_t i = 0; i < content_.lines.size(); ++i) {
    line_labels_.push_back(AddControl(L"STATIC", content_.lines[i].text, kLabel,
                                      kFirstLineId + static_cast<int>(i),
                                      text_font_));
  }
  if (!content_.message.empty()) {
    if (content_.placement == Placement::kFill) {
      message_ = AddControl(L"STATIC", content_.message, kLabel, kMessageId,
                            text_font_);
    } else {
      // Selectable; a scroll bar only if the message is too long.
      HDC dc = GetDC(window_);
      const int width = Scale(content_.width) - 2 * Scale(kPadding);
      const bool scrolls = TextHeight(dc, text_font_, content_.message, width) >
                           Scale(kMaxMessageHeight);
      ReleaseDC(window_, dc);
      message_ = AddControl(L"EDIT", EditLineBreaks(content_.message),
                            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                                (scrolls ? WS_VSCROLL : 0),
                            kMessageId, text_font_);
      SendMessageW(message_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
      if (theme_.dark) {
        SetWindowTheme(message_, L"DarkMode_Explorer", nullptr);
      }
    }
  }
  if (!content_.detail.empty()) {
    detail_ =
        AddControl(L"STATIC", content_.detail, kLabel, kDetailId, small_font_);
  }
  if (content_.input) {
    input_ = AddControl(L"EDIT", *content_.input,
                        ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, kInputId,
                        text_font_);
    SendMessageW(input_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(Scale(4), Scale(4)));
    SetAccessibleName(input_, content_.input_name.c_str());
    focus_order_.push_back(input_);
  }
  if (!content_.checkbox.empty()) {
    // The label is its own control: a themed checkbox draws its text black
    // even in dark mode.
    checkbox_ = AddControl(L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP,
                           kCheckboxId, text_font_);
    SetAccessibleName(checkbox_, content_.checkbox.c_str());
    if (theme_.dark) {
      SetWindowTheme(checkbox_, L"DarkMode_Explorer", nullptr);
    }
    checkbox_label_ =
        AddControl(L"STATIC", content_.checkbox, kLabel | SS_NOTIFY,
                   kCheckboxLabelId, text_font_);
    focus_order_.push_back(checkbox_);
  }
  for (size_t i = 0; i < content_.buttons.size(); ++i) {
    HWND button =
        AddControl(L"BUTTON", content_.buttons[i], BS_OWNERDRAW | WS_TABSTOP,
                   kFirstButtonId + static_cast<int>(i), text_font_);
    buttons_.push_back(button);
    focus_order_.push_back(button);
  }
  if (close_) {
    focus_order_.push_back(close_);
  }
  for (HWND control : {input_, message_, checkbox_, close_}) {
    if (control) {
      SetWindowSubclass(control, &TabPanel::ControlProc, 0,
                        reinterpret_cast<DWORD_PTR>(this));
    }
  }
  for (HWND button : buttons_) {
    SetWindowSubclass(button, &TabPanel::ControlProc, 0,
                      reinterpret_cast<DWORD_PTR>(this));
  }
}

int TabPanel::Layout(int x, int y, int width, bool apply) {
  const int top = y;
  auto put = [apply](HWND control, int left, int top_edge, int w, int h) {
    if (apply && control) {
      SetWindowPos(control, nullptr, left, top_edge, w, h,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }
  };
  if (apply) {
    glyphs_.clear();
  }
  HDC dc = GetDC(window_);
  if (!content_.icon.empty()) {
    const int size = Scale(48);
    if (apply) {
      glyphs_.push_back({{x, y, x + size, y + size},
                         content_.icon,
                         icon_font_,
                         theme_.text_dim});
    }
    y += size + Scale(12);
  }
  if (title_) {
    const int close = close_ ? Scale(28) : 0;
    const int height =
        TextHeight(dc, title_font_, content_.title, width - close);
    put(title_, x, y, width - close, height);
    put(close_, x + width - Scale(24), y - Scale(2), Scale(24), Scale(24));
    y += height + Scale(12);
  }
  for (size_t i = 0; i < line_labels_.size(); ++i) {
    const int indent = Scale(28);
    const int height = std::max(
        TextHeight(dc, text_font_, content_.lines[i].text, width - indent),
        Scale(20));
    if (apply) {
      glyphs_.push_back({{x, y, x + Scale(20), y + Scale(20)},
                         content_.lines[i].glyph,
                         glyph_font_,
                         theme_.text});
    }
    put(line_labels_[i], x + indent, y + Scale(1), width - indent, height);
    y += height + Scale(6);
  }
  if (!line_labels_.empty()) {
    y += Scale(6);
  }
  if (message_) {
    int height = TextHeight(dc, text_font_, content_.message, width);
    if (content_.placement != Placement::kFill) {
      height = std::min(height + Scale(2), Scale(kMaxMessageHeight));
    }
    put(message_, x, y, width, height);
    y += height + Scale(10);
  }
  if (detail_) {
    const int height = TextHeight(dc, small_font_, content_.detail, width);
    put(detail_, x, y, width, height);
    y += height + Scale(10);
  }
  if (content_.placement == Placement::kFill && (message_ || detail_)) {
    y += Scale(14);
  }
  if (input_) {
    const int height = TextHeight(dc, text_font_, L"Ag", width) + Scale(10);
    put(input_, x, y, width, height);
    y += height + Scale(12);
  }
  if (checkbox_) {
    const int box = Scale(16);
    const int indent = Scale(24);
    const int label =
        TextHeight(dc, text_font_, content_.checkbox, width - indent);
    const int row = std::max(box, label);
    put(checkbox_, x, y + (row - box) / 2, box, box);
    put(checkbox_label_, x + indent, y + (row - label) / 2, width - indent,
        label);
    y += row + Scale(14);
  }
  if (!buttons_.empty()) {
    y += Scale(4);
    if (content_.stacked_buttons) {
      for (HWND button : buttons_) {
        put(button, x, y, width, Scale(32));
        y += Scale(38);
      }
      y -= Scale(6);
    } else {
      std::vector<int> widths;
      int total = 0;
      for (const std::wstring& title : content_.buttons) {
        widths.push_back(
            std::max(Scale(80), TextWidth(dc, text_font_, title) + Scale(32)));
        total += widths.back();
      }
      total += Scale(8) * static_cast<int>(widths.size() - 1);
      // Right-aligned in dialogs (Windows), under the text on the crash page.
      int left = content_.placement == Placement::kFill ? x : x + width - total;
      for (size_t i = 0; i < buttons_.size(); ++i) {
        put(buttons_[i], left, y, widths[i], Scale(32));
        left += widths[i] + Scale(8);
      }
      y += Scale(32);
    }
  } else if (y > top) {
    y -= Scale(10);
  }
  ReleaseDC(window_, dc);
  return y - top;
}

void TabPanel::Place() {
  if (!window_) {
    return;
  }
  RECT parent = {};
  GetClientRect(parent_, &parent);
  const int parent_width = parent.right;
  const int parent_height = parent.bottom;
  switch (content_.placement) {
    case Placement::kFill: {
      SetWindowPos(window_, HWND_TOP, 0, 0, parent_width, parent_height,
                   SWP_NOACTIVATE);
      const int width = std::max(
          Scale(120),
          std::min(Scale(content_.width), parent_width - 2 * Scale(kPadding)));
      const int height = Layout(0, 0, width, false);
      const int y =
          std::max(Scale(kPadding), (parent_height - height) * 45 / 100);
      Layout((parent_width - width) / 2, y, width, true);
      InvalidateRect(window_, nullptr, TRUE);
      break;
    }
    case Placement::kTopCenter: {
      const int width = Scale(content_.width);
      SetWindowPos(window_, HWND_TOP, (parent_width - width) / 2,
                   Scale(kTopOffset), width, height_, SWP_NOACTIVATE);
      break;
    }
    case Placement::kTopLeft:
      SetWindowPos(window_, HWND_TOP, Scale(kPromptLeft), Scale(kPromptTop),
                   Scale(content_.width), height_, SWP_NOACTIVATE);
      break;
  }
}

void TabPanel::Focus() {
  if (!window_ || answered_) {
    return;
  }
  if (input_) {
    SetFocus(input_);
    SendMessageW(input_, EM_SETSEL, 0, -1);
  } else if (content_.first_is_default && !buttons_.empty()) {
    SetFocus(buttons_.front());
  } else if (!focus_order_.empty()) {
    SetFocus(focus_order_.front());
  }
}

void TabPanel::MoveFocus(bool forward) {
  if (focus_order_.empty()) {
    return;
  }
  const auto current =
      std::find(focus_order_.begin(), focus_order_.end(), GetFocus());
  const auto count = static_cast<std::ptrdiff_t>(focus_order_.size());
  std::ptrdiff_t index = 0;
  if (current != focus_order_.end()) {
    index =
        (current - focus_order_.begin() + (forward ? 1 : count - 1)) % count;
  }
  SetFocus(focus_order_[static_cast<size_t>(index)]);
}

void TabPanel::Finish(int button) {
  if (answered_) {
    return;
  }
  answered_ = true;
  Answer answer;
  answer.button = button;
  if (input_) {
    answer.input = WindowText(input_);
  }
  if (checkbox_) {
    answer.checked = SendMessageW(checkbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
  }
  ShowWindow(window_, SW_HIDE);
  Done done = std::move(done_);
  done_ = nullptr;
  if (done) {
    // Not from inside this panel's window procedure: `done` may delete it.
    CefPostTask(TID_UI, new FunctionTask([done, answer] { done(answer); }));
  }
}

void TabPanel::Paint() {
  PAINTSTRUCT paint = {};
  HDC dc = BeginPaint(window_, &paint);
  RECT client = {};
  GetClientRect(window_, &client);
  FillRect(dc, &client, background_brush_);
  if (content_.placement != Placement::kFill) {
    HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
    HGDIOBJ brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetDCPenColor(dc, theme_.separator);
    const int radius = Scale(kCornerRadius);
    RoundRect(dc, 0, 0, client.right, client.bottom, radius, radius);
    SelectObject(dc, brush);
    SelectObject(dc, pen);
  }
  SetBkMode(dc, TRANSPARENT);
  for (const Glyph& glyph : glyphs_) {
    HGDIOBJ previous = SelectObject(dc, glyph.font);
    SetTextColor(dc, glyph.color);
    RECT area = glyph.rect;
    DrawTextW(dc, glyph.text.c_str(), -1, &area,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
  }
  EndPaint(window_, &paint);
}

void TabPanel::DrawButton(const DRAWITEMSTRUCT& item) {
  HDC dc = item.hDC;
  RECT area = item.rcItem;
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  FillRect(dc, &area, background_brush_);
  SetBkMode(dc, TRANSPARENT);
  HGDIOBJ previous_font = nullptr;
  if (item.hwndItem == close_) {
    if (pressed) {
      SetDCBrushColor(dc, theme_.hover);
      FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    previous_font = SelectObject(dc, small_glyph_font_);
    SetTextColor(dc, theme_.text);
    DrawTextW(dc, kCloseGlyph, -1, &area,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  } else {
    // The default button in the system's accent colour, as in Windows'
    // own dialogs.
    const bool is_default = content_.first_is_default && !buttons_.empty() &&
                            item.hwndItem == buttons_.front();
    const COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);
    COLORREF fill = pressed ? theme_.hover : theme_.field;
    if (is_default) {
      fill = accent;
    }
    HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
    HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCPenColor(dc, is_default ? accent : theme_.separator);
    SetDCBrushColor(dc, fill);
    const int radius = Scale(8);
    RoundRect(dc, area.left, area.top, area.right, area.bottom, radius, radius);
    SelectObject(dc, brush);
    SelectObject(dc, pen);
    previous_font = SelectObject(dc, text_font_);
    SetTextColor(dc, is_default ? RGB(255, 255, 255) : theme_.text);
    const std::wstring title = WindowText(item.hwndItem);
    DrawTextW(dc, title.c_str(), -1, &area,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }
  SelectObject(dc, previous_font);
  if ((item.itemState & ODS_FOCUS) != 0 &&
      (item.itemState & ODS_NOFOCUSRECT) == 0) {
    RECT focus = area;
    InflateRect(&focus, -Scale(3), -Scale(3));
    DrawFocusRect(dc, &focus);
  }
}

// static
LRESULT CALLBACK TabPanel::PanelProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  }
  auto* self =
      reinterpret_cast<TabPanel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self || !self->window_) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT TabPanel::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_PAINT:
      Paint();
      return 0;
    case WM_ERASEBKGND:
      return 1;  // Painted in WM_PAINT.
    case WM_DRAWITEM:
      DrawButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
      return TRUE;
    case WM_CTLCOLORSTATIC: {  // Labels, the checkbox, the read-only message.
      HDC dc = reinterpret_cast<HDC>(wparam);
      const auto control = reinterpret_cast<HWND>(lparam);
      SetTextColor(dc, control == detail_ ? theme_.text_dim : theme_.text);
      SetBkColor(dc, theme_.toolbar);
      return reinterpret_cast<LRESULT>(background_brush_);
    }
    case WM_CTLCOLOREDIT: {  // The prompt's text field.
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, theme_.text);
      SetBkColor(dc, theme_.field);
      return reinterpret_cast<LRESULT>(field_brush_);
    }
    case WM_COMMAND: {
      const int id = LOWORD(wparam);
      const int code = HIWORD(wparam);
      if (id == kCloseId && code == BN_CLICKED) {
        Finish(kClosed);
        return 0;
      }
      if (id == kCheckboxLabelId && code == STN_CLICKED) {
        const bool checked =
            SendMessageW(checkbox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
        SendMessageW(checkbox_, BM_SETCHECK,
                     checked ? BST_UNCHECKED : BST_CHECKED, 0);
        return 0;
      }
      const int button = id - kFirstButtonId;
      if (code == BN_CLICKED && button >= 0 &&
          button < static_cast<int>(buttons_.size())) {
        Finish(button);
        return 0;
      }
      break;
    }
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

// static
LRESULT CALLBACK TabPanel::ControlProc(HWND window,
                                       UINT message,
                                       WPARAM wparam,
                                       LPARAM lparam,
                                       UINT_PTR /*id*/,
                                       DWORD_PTR data) {
  auto* self = reinterpret_cast<TabPanel*>(data);
  switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      const int key = static_cast<int>(wparam);
      const bool control = KeyDown(VK_CONTROL);
      const bool alt = KeyDown(VK_MENU);
      const bool shift = KeyDown(VK_SHIFT);
      if (key == VK_RETURN && !control && !alt) {
        // Enter presses the focused button, elsewhere the default one.
        if (window == self->close_) {
          self->Finish(kClosed);
          return 0;
        }
        for (size_t i = 0; i < self->buttons_.size(); ++i) {
          if (window == self->buttons_[i]) {
            self->Finish(static_cast<int>(i));
            return 0;
          }
        }
        if (self->content_.first_is_default && !self->buttons_.empty()) {
          self->Finish(0);
        }
        return 0;
      }
      if (key == VK_ESCAPE) {
        if (self->content_.escape_answer) {
          self->Finish(*self->content_.escape_answer);
        }
        return 0;
      }
      if (key == VK_TAB && !control && !alt) {
        self->MoveFocus(!shift);
        return 0;
      }
      if (control || alt || (key >= VK_F1 && key <= VK_F24)) {
        // A copy: the shortcut may close the tab and so this panel.
        Shortcuts shortcuts = self->shortcuts_;
        if (shortcuts && shortcuts(key, control, alt, shift)) {
          return 0;
        }
      }
      break;
    }
    case WM_CHAR:
      // Enter, Escape and Tab are handled above; without this edits beep.
      if (wparam == L'\r' || wparam == 0x1B || wparam == L'\t') {
        return 0;
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &TabPanel::ControlProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

}  // namespace brouk::win
