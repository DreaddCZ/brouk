#include "platform/win/command_palette.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <utility>

#include "platform/win/controls.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukCommandPalette";
constexpr int kWidth = 560;       // DIPs.
constexpr int kRowHeight = 40;    // Title over detail.
constexpr int kFieldHeight = 44;  // The search field's row.
constexpr int kMaxRows = 10;
constexpr std::size_t kMaxResults = 50;

// Glyphs of the "Segoe MDL2 Assets" icon font, by kind.
const wchar_t* GlyphOf(PaletteItem::Kind kind) {
  switch (kind) {
    case PaletteItem::Kind::kTab:
      return L"";  // Page.
    case PaletteItem::Kind::kAction:
      return L"";  // CommandPrompt.
    case PaletteItem::Kind::kBookmark:
      return L"";  // FavoriteStar.
    case PaletteItem::Kind::kSetting:
      return L"";  // Setting.
    case PaletteItem::Kind::kHelp:
      return L"";  // Help.
  }
  return L"";
}

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

}  // namespace

CommandPalette::CommandPalette() = default;

CommandPalette::~CommandPalette() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  if (field_brush_) {
    DeleteObject(field_brush_);
  }
}

int CommandPalette::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(owner_)), 96);
}

int CommandPalette::RowHeight() const {
  return Scale(kRowHeight);
}

int CommandPalette::FieldHeight() const {
  return Scale(kFieldHeight);
}

void CommandPalette::Show(HWND owner,
                          int top,
                          std::vector<PaletteEntry> entries,
                          const Theme& theme,
                          HFONT text_font,
                          HFONT icon_font) {
  if (!RegisterWindowClass()) {
    return;
  }
  Close();
  owner_ = owner;
  top_ = top;
  entries_ = std::move(entries);
  items_.clear();
  for (const PaletteEntry& entry : entries_) {
    items_.push_back(
        {.kind = entry.kind, .title = entry.title, .detail = entry.detail});
  }
  theme_ = theme;
  text_font_ = text_font;
  icon_font_ = icon_font;
  if (field_brush_) {
    DeleteObject(field_brush_);
  }
  field_brush_ = CreateSolidBrush(theme_.field);
  if (!window_) {
    window_ =
        CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Command palette",
                        WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, 0, 0, 0, 0,
                        owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window_) {
      return;
    }
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(window_, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(&CommandPalette::WindowProc));
    field_ = CreateWindowExW(
        0, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0,
        window_, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(field_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(
                     L"Search tabs, actions, bookmarks, settings and help"));
    SetAccessibleName(field_, L"Command palette");
    SetWindowSubclass(field_, &CommandPalette::FieldProc, 0,
                      reinterpret_cast<DWORD_PTR>(this));
  }
  SendMessageW(field_, WM_SETFONT, reinterpret_cast<WPARAM>(text_font_), FALSE);
  SetWindowTextW(field_, L"");
  width_ = Scale(kWidth);
  Filter();
  ShowWindow(window_, SW_SHOW);
  SetForegroundWindow(window_);
  SetFocus(field_);
}

void CommandPalette::Close() {
  if (window_ && IsWindowVisible(window_)) {
    ShowWindow(window_, SW_HIDE);
    if (owner_ && IsWindow(owner_)) {
      SetForegroundWindow(owner_);
    }
  }
}

void CommandPalette::Filter() {
  const std::string query = WideToUtf8(WindowText(field_));
  shown_ = RankPaletteItems(items_, query, kMaxResults);
  selected_ = 0;
  RECT owner = {};
  GetWindowRect(owner_, &owner);
  const int rows = std::min(static_cast<int>(shown_.size()), kMaxRows);
  const int height = FieldHeight() + rows * RowHeight() + Scale(8);
  const int x = owner.left + ((owner.right - owner.left) - width_) / 2;
  SetWindowPos(window_, HWND_TOP, x, owner.top + top_, width_, height,
               SWP_NOACTIVATE);
  const int margin = Scale(14);
  const int edit_height = Scale(22);
  SetWindowPos(field_, nullptr, margin, (FieldHeight() - edit_height) / 2,
               width_ - 2 * margin, edit_height, SWP_NOZORDER | SWP_NOACTIVATE);
  InvalidateRect(window_, nullptr, TRUE);
}

void CommandPalette::Select(int row) {
  const int count = std::min(static_cast<int>(shown_.size()), kMaxRows);
  if (count == 0) {
    return;
  }
  selected_ = std::clamp(row, 0, count - 1);
  InvalidateRect(window_, nullptr, FALSE);
}

void CommandPalette::Run(int row) {
  if (row < 0 || static_cast<std::size_t>(row) >= shown_.size()) {
    return;
  }
  // The entry runs after the palette has closed (it may open windows).
  std::function<void()> run =
      entries_[shown_[static_cast<std::size_t>(row)]].run;
  Close();
  if (run) {
    run();
  }
}

int CommandPalette::RowAt(int y) const {
  if (y < FieldHeight()) {
    return -1;
  }
  const int row = (y - FieldHeight()) / RowHeight();
  return row < std::min(static_cast<int>(shown_.size()), kMaxRows) ? row : -1;
}

void CommandPalette::Paint() {
  PAINTSTRUCT paint = {};
  HDC dc = BeginPaint(window_, &paint);
  RECT client = {};
  GetClientRect(window_, &client);
  SetDCBrushColor(dc, theme_.toolbar);
  FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  RECT field = {Scale(8), Scale(6), client.right - Scale(8),
                FieldHeight() - Scale(6)};
  SetDCBrushColor(dc, theme_.field);
  FillRect(dc, &field, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  SetBkMode(dc, TRANSPARENT);
  const int margin = Scale(14);
  const int icon = Scale(28);
  const int count = std::min(static_cast<int>(shown_.size()), kMaxRows);
  for (int i = 0; i < count; ++i) {
    const PaletteEntry& entry = entries_[shown_[static_cast<std::size_t>(i)]];
    RECT row = {0, FieldHeight() + i * RowHeight(), client.right,
                FieldHeight() + (i + 1) * RowHeight()};
    if (i == selected_) {
      SetDCBrushColor(dc, theme_.hover);
      FillRect(dc, &row, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    RECT glyph = {margin, row.top, margin + icon, row.bottom};
    SelectObject(dc, icon_font_);
    SetTextColor(dc, theme_.text_dim);
    DrawTextW(dc, GlyphOf(entry.kind), -1, &glyph,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(dc, text_font_);
    RECT shortcut = {row.left, row.top, row.right - margin, row.bottom};
    int text_right = row.right - margin;
    if (!entry.shortcut.empty()) {
      SetTextColor(dc, theme_.text_dim);
      RECT measured = shortcut;
      DrawTextW(dc, entry.shortcut.c_str(), -1, &measured,
                DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
      DrawTextW(dc, entry.shortcut.c_str(), -1, &shortcut,
                DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
      text_right -= (measured.right - measured.left) + Scale(12);
    }
    const int middle = (row.top + row.bottom) / 2;
    RECT title = {glyph.right, row.top + Scale(3), text_right,
                  middle + Scale(2)};
    SetTextColor(dc, theme_.text);
    DrawTextW(dc, Utf8ToWide(entry.title).c_str(), -1, &title,
              DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX | DT_END_ELLIPSIS);
    RECT detail = {glyph.right, middle + Scale(2), text_right,
                   row.bottom - Scale(3)};
    SetTextColor(dc, theme_.text_dim);
    DrawTextW(dc, Utf8ToWide(entry.detail).c_str(), -1, &detail,
              DT_SINGLELINE | DT_TOP | DT_NOPREFIX | DT_END_ELLIPSIS);
  }
  EndPaint(window_, &paint);
}

LRESULT CALLBACK CommandPalette::WindowProc(HWND window,
                                            UINT message,
                                            WPARAM wparam,
                                            LPARAM lparam) {
  auto* self = reinterpret_cast<CommandPalette*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// The field keeps the keyboard: Up/Down move the selection, Enter runs it,
// Escape closes the palette.
LRESULT CALLBACK CommandPalette::FieldProc(HWND window,
                                           UINT message,
                                           WPARAM wparam,
                                           LPARAM lparam,
                                           UINT_PTR /*id*/,
                                           DWORD_PTR data) {
  auto* self = reinterpret_cast<CommandPalette*>(data);
  switch (message) {
    case WM_KEYDOWN:
      switch (wparam) {
        case VK_DOWN:
          self->Select(self->selected_ + 1);
          return 0;
        case VK_UP:
          self->Select(self->selected_ - 1);
          return 0;
        case VK_RETURN:
          self->Run(self->selected_);
          return 0;
        case VK_ESCAPE:
          self->Close();
          return 0;
        default:
          break;
      }
      break;
    case WM_CHAR:
      if (wparam == L'\r' || wparam == 0x1B) {
        return 0;  // Handled above; without this the edit beeps.
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &CommandPalette::FieldProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT CommandPalette::HandleMessage(UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam) {
  switch (message) {
    case WM_COMMAND:
      if (reinterpret_cast<HWND>(lparam) == field_ &&
          HIWORD(wparam) == EN_CHANGE) {
        Filter();
        return 0;
      }
      break;
    case WM_ACTIVATE:
      // Clicking elsewhere closes it.
      if (LOWORD(wparam) == WA_INACTIVE) {
        ShowWindow(window_, SW_HIDE);
      }
      break;
    case WM_ERASEBKGND:
      return 1;  // Painted in WM_PAINT.
    case WM_PAINT:
      Paint();
      return 0;
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, theme_.text);
      SetBkColor(dc, theme_.field);
      return reinterpret_cast<LRESULT>(field_brush_);
    }
    case WM_MOUSEMOVE:
      if (const int row = RowAt(GET_Y_LPARAM(lparam));
          row >= 0 && row != selected_) {
        Select(row);
      }
      return 0;
    case WM_LBUTTONUP:
      if (const int row = RowAt(GET_Y_LPARAM(lparam)); row >= 0) {
        Run(row);
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
