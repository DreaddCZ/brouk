#include "platform/win/link_picker.h"

#include <windowsx.h>

#include <algorithm>
#include <utility>

#include "platform/win/controls.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukLinkPicker";
constexpr int kWidth = 540;
constexpr int kMargin = 18;
constexpr int kProfileRowHeight = 38;
constexpr int kWindowRowHeight = 100;
constexpr int kThumbnailWidth = 144;
constexpr int kThumbnailHeight = 86;
constexpr int kVisibleProfileRows = 9;
constexpr int kVisibleWindowRows = 4;
constexpr wchar_t kNewWindowGlyph[] = L"";  // OpenInNewWindow.

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

void FillRectColor(HDC dc, const RECT& rect, COLORREF color) {
  SetDCBrushColor(dc, color);
  FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void Dot(HDC dc, int x, int y, int size, COLORREF color) {
  SetDCBrushColor(dc, color);
  SetDCPenColor(dc, color);
  HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
  HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
  Ellipse(dc, x, y, x + size, y + size);
  SelectObject(dc, pen);
  SelectObject(dc, brush);
}

}  // namespace

LinkPicker::LinkPicker() = default;

LinkPicker::~LinkPicker() {
  ClearThumbnails();
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  for (HFONT font : {title_font_, text_font_, small_font_, icon_font_}) {
    if (font) {
      DeleteObject(font);
    }
  }
}

int LinkPicker::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(window_)), 96);
}

void LinkPicker::UpdateFonts() {
  for (HFONT font : {title_font_, text_font_, small_font_, icon_font_}) {
    if (font) {
      DeleteObject(font);
    }
  }
  const UINT dpi = GetDpiForWindow(window_);
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                             0, dpi);
  LOGFONTW font = metrics.lfMessageFont;
  text_font_ = CreateFontIndirectW(&font);
  font.lfHeight = MulDiv(font.lfHeight, 115, 100);
  font.lfWeight = FW_SEMIBOLD;
  title_font_ = CreateFontIndirectW(&font);
  font = metrics.lfMessageFont;
  font.lfHeight = MulDiv(font.lfHeight, 88, 100);
  small_font_ = CreateFontIndirectW(&font);
  LOGFONTW icon = {};
  icon.lfHeight = -MulDiv(16, static_cast<int>(dpi), 96);
  wcscpy_s(icon.lfFaceName, L"Segoe MDL2 Assets");
  icon_font_ = CreateFontIndirectW(&icon);
}

void LinkPicker::Show(const std::wstring& url,
                      const std::wstring& via,
                      const std::wstring& source,
                      std::vector<LinkPickerProfile> profiles,
                      std::optional<std::wstring> rule_pattern,
                      WindowsFor windows_for,
                      HMONITOR monitor,
                      Done done) {
  if (visible()) {
    Finish(LinkPickerResult{.cancelled = true, .profile = {}});
  }
  if (!RegisterWindowClass()) {
    done(LinkPickerResult{.cancelled = true, .profile = {}});
    return;
  }
  if (!window_) {
    window_ =
        CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kWindowClass,
                        L"Open link", WS_POPUP | WS_BORDER, 0, 0, 0, 0, nullptr,
                        nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window_) {
      done(LinkPickerResult{.cancelled = true, .profile = {}});
      return;
    }
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(window_, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(&LinkPicker::WindowProc));
  }
  theme_ = CurrentTheme();
  UpdateFonts();
  url_ = url;
  source_ = via.empty() ? source : source + L" · via " + via;
  profiles_ = std::move(profiles);
  rule_pattern_ = std::move(rule_pattern);
  windows_for_ = std::move(windows_for);
  done_ = std::move(done);
  always_ = false;
  if (profiles_.size() > 1) {
    ShowProfileStep(0);
  } else if (profiles_.size() == 1) {
    chosen_ = profiles_.front().id;
    if (!ShowWindowStep()) {
      Finish(LinkPickerResult{.cancelled = false, .profile = chosen_});
      return;
    }
  } else {
    Finish(LinkPickerResult{.cancelled = true, .profile = {}});
    return;
  }
  // Centred on the screen the user is looking at, a little above the
  // middle, as on macOS.
  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  GetMonitorInfoW(
      monitor ? monitor : MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY),
      &info);
  const RECT& area = info.rcWork;
  const int width = Scale(kWidth);
  Layout();
  RECT frame = {};
  GetWindowRect(window_, &frame);
  const int height = frame.bottom - frame.top;
  SetWindowPos(
      window_, HWND_TOPMOST, (area.left + area.right - width) / 2,
      (area.top + area.bottom - height) / 2 - (area.bottom - area.top) / 10,
      width, height, SWP_SHOWWINDOW);
  SetForegroundWindow(window_);
  SetFocus(window_);
  UpdateThumbnails();
}

void LinkPicker::Cancel() {
  if (visible()) {
    Finish(LinkPickerResult{.cancelled = true, .profile = {}});
  }
}

void LinkPicker::ShowProfileStep(int selected) {
  ClearThumbnails();
  window_step_ = false;
  windows_.clear();
  selected_ = std::clamp(selected, 0, std::max(0, RowCount() - 1));
  first_row_ = 0;
  if (IsWindowVisible(window_)) {
    Layout();
  }
}

bool LinkPicker::ShowWindowStep() {
  std::vector<LinkPickerWindow> windows =
      windows_for_ ? windows_for_(chosen_) : std::vector<LinkPickerWindow>{};
  if (windows.empty()) {
    return false;
  }
  ClearThumbnails();
  windows_ = std::move(windows);
  window_step_ = true;
  selected_ = 0;
  first_row_ = 0;
  for (const LinkPickerWindow& window : windows_) {
    Thumbnail thumbnail;
    if (window.window && SUCCEEDED(DwmRegisterThumbnail(window_, window.window,
                                                        &thumbnail.handle))) {
      DwmQueryThumbnailSourceSize(thumbnail.handle, &thumbnail.source);
    }
    thumbnails_.push_back(thumbnail);
  }
  if (IsWindowVisible(window_)) {
    Layout();
    UpdateThumbnails();
  }
  return true;
}

void LinkPicker::ClearThumbnails() {
  for (const Thumbnail& thumbnail : thumbnails_) {
    if (thumbnail.handle) {
      DwmUnregisterThumbnail(thumbnail.handle);
    }
  }
  thumbnails_.clear();
}

int LinkPicker::RowCount() const {
  return window_step_ ? static_cast<int>(windows_.size()) + 1
                      : static_cast<int>(profiles_.size());
}

int LinkPicker::RowHeight() const {
  return Scale(window_step_ ? kWindowRowHeight : kProfileRowHeight);
}

int LinkPicker::VisibleRows() const {
  return std::min(RowCount(),
                  window_step_ ? kVisibleWindowRows : kVisibleProfileRows);
}

// The parts top to bottom: the link, its source, the step, the rows, the
// "Always…" checkbox, the hint and the buttons. Sizes the window.
void LinkPicker::Layout() {
  const int margin = Scale(kMargin);
  int y = margin + Scale(24) + Scale(22);  // Title and URL.
  if (!source_.empty()) {
    y += Scale(20);
  }
  y += Scale(12) + Scale(26);  // The step's question.
  list_top_ = y;
  y += VisibleRows() * RowHeight() + Scale(8);
  always_rect_ = {};
  if (!window_step_ && rule_pattern_ && profiles_.size() > 1) {
    always_rect_ = {margin, y, Scale(kWidth) - margin, y + Scale(24)};
    y += Scale(28);
  }
  y += Scale(20);  // The hint.
  const int button_height = Scale(28);
  const int button_width = Scale(84);
  cancel_rect_ = {Scale(kWidth) - margin - button_width, y,
                  Scale(kWidth) - margin, y + button_height};
  back_rect_ = {};
  if (window_step_ && profiles_.size() > 1) {
    back_rect_ = {cancel_rect_.left - Scale(8) - button_width, y,
                  cancel_rect_.left - Scale(8), y + button_height};
  }
  y += button_height + margin;
  RECT frame = {0, 0, Scale(kWidth), y};
  AdjustWindowRectEx(&frame, WS_POPUP | WS_BORDER, FALSE,
                     WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
  SetWindowPos(window_, nullptr, 0, 0, frame.right - frame.left,
               frame.bottom - frame.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  InvalidateRect(window_, nullptr, FALSE);
  UpdateThumbnails();
}

RECT LinkPicker::RowRect(int row) const {
  const int margin = Scale(kMargin);
  const int top = list_top_ + (row - first_row_) * RowHeight();
  return {margin - Scale(6), top, Scale(kWidth) - margin + Scale(6),
          top + RowHeight()};
}

int LinkPicker::RowAt(POINT point) const {
  for (int row = first_row_; row < first_row_ + VisibleRows(); ++row) {
    const RECT rect = RowRect(row);
    if (PtInRect(&rect, point)) {
      return row;
    }
  }
  return -1;
}

void LinkPicker::UpdateThumbnails() {
  for (std::size_t i = 0; i < thumbnails_.size(); ++i) {
    const Thumbnail& thumbnail = thumbnails_[i];
    if (!thumbnail.handle) {
      continue;
    }
    const int row = static_cast<int>(i);
    const bool shown =
        window_step_ && row >= first_row_ && row < first_row_ + VisibleRows();
    const RECT rect = RowRect(row);
    // The thumbnail keeps its window's shape inside its box.
    RECT box = {rect.left + Scale(36), rect.top + Scale(7),
                rect.left + Scale(36) + Scale(kThumbnailWidth),
                rect.top + Scale(7) + Scale(kThumbnailHeight)};
    if (thumbnail.source.cx > 0 && thumbnail.source.cy > 0) {
      const double scale = std::min(
          static_cast<double>(box.right - box.left) / thumbnail.source.cx,
          static_cast<double>(box.bottom - box.top) / thumbnail.source.cy);
      const int width = static_cast<int>(thumbnail.source.cx * scale);
      const int height = static_cast<int>(thumbnail.source.cy * scale);
      box.left += (box.right - box.left - width) / 2;
      box.top += (box.bottom - box.top - height) / 2;
      box.right = box.left + width;
      box.bottom = box.top + height;
    }
    DWM_THUMBNAIL_PROPERTIES properties = {};
    properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE |
                         DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    properties.rcDestination = box;
    properties.fVisible = shown ? TRUE : FALSE;
    properties.opacity = 255;
    properties.fSourceClientAreaOnly = FALSE;
    DwmUpdateThumbnailProperties(thumbnail.handle, &properties);
  }
}

void LinkPicker::Paint() {
  PAINTSTRUCT paint = {};
  HDC dc = BeginPaint(window_, &paint);
  RECT client = {};
  GetClientRect(window_, &client);
  FillRectColor(dc, client, theme_.toolbar);
  SetBkMode(dc, TRANSPARENT);
  const int margin = Scale(kMargin);
  const int right = client.right - margin;
  auto text = [&](const std::wstring& value, HFONT font, COLORREF color,
                  RECT area, UINT format) {
    SelectObject(dc, font);
    SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), -1, &area,
              format | DT_SINGLELINE | DT_NOPREFIX | DT_VCENTER);
  };
  int y = margin;
  text(L"Open link", small_font_, theme_.text_dim,
       {margin, y, right, y + Scale(24)}, DT_LEFT);
  y += Scale(24);
  text(url_, text_font_, theme_.text, {margin, y, right, y + Scale(22)},
       DT_LEFT | DT_PATH_ELLIPSIS);
  y += Scale(22);
  if (!source_.empty()) {
    text(source_, small_font_, theme_.text_dim,
         {margin, y, right, y + Scale(20)}, DT_LEFT | DT_END_ELLIPSIS);
    y += Scale(20);
  }
  y += Scale(12);
  std::wstring step = L"Open in which profile?";
  if (window_step_) {
    step = L"Open in which window?";
    for (const LinkPickerProfile& profile : profiles_) {
      if (profile.id == chosen_) {
        step = L"Open in which " + profile.name + L" window?";
      }
    }
  }
  text(step, title_font_, theme_.text, {margin, y, right, y + Scale(26)},
       DT_LEFT | DT_END_ELLIPSIS);
  // The rows.
  for (int row = first_row_; row < first_row_ + VisibleRows(); ++row) {
    const RECT rect = RowRect(row);
    if (row == selected_) {
      FillRectColor(dc, rect, theme_.hover);
    }
    const std::wstring key = row < 9 ? std::to_wstring(row + 1) : L"";
    text(key, small_font_, theme_.text_dim,
         {rect.left + Scale(8), rect.top, rect.left + Scale(30), rect.bottom},
         DT_LEFT);
    if (!window_step_) {
      const LinkPickerProfile& profile =
          profiles_[static_cast<std::size_t>(row)];
      const int dot = Scale(12);
      Dot(dc, rect.left + Scale(36), (rect.top + rect.bottom - dot) / 2, dot,
          profile.color);
      text(
          profile.name, text_font_, theme_.text,
          {rect.left + Scale(58), rect.top, rect.right - Scale(8), rect.bottom},
          DT_LEFT | DT_END_ELLIPSIS);
      continue;
    }
    const int text_left =
        rect.left + Scale(36) + Scale(kThumbnailWidth) + Scale(12);
    if (row == static_cast<int>(windows_.size())) {
      text(kNewWindowGlyph, icon_font_, theme_.text_dim,
           {rect.left + Scale(36), rect.top,
            rect.left + Scale(36) + Scale(kThumbnailWidth), rect.bottom},
           DT_CENTER);
      text(L"New window", text_font_, theme_.text,
           {text_left, rect.top, rect.right - Scale(8), rect.bottom}, DT_LEFT);
      continue;
    }
    const LinkPickerWindow& window = windows_[static_cast<std::size_t>(row)];
    if (!thumbnails_[static_cast<std::size_t>(row)].handle ||
        IsIconic(window.window)) {
      // No live picture (a minimised window): a frame in its place.
      RECT box = {rect.left + Scale(36), rect.top + Scale(7),
                  rect.left + Scale(36) + Scale(kThumbnailWidth),
                  rect.top + Scale(7) + Scale(kThumbnailHeight)};
      SetDCBrushColor(dc, theme_.separator);
      FrameRect(dc, &box, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    const int middle = (rect.top + rect.bottom) / 2;
    text(window.title.empty() ? L"New Tab" : window.title, text_font_,
         theme_.text,
         {text_left, middle - Scale(24), rect.right - Scale(8), middle},
         DT_LEFT | DT_END_ELLIPSIS);
    // The window's groups as chips: a dot in the profile's colour, the
    // name and the number of tabs.
    int x = text_left;
    SelectObject(dc, small_font_);
    for (const LinkPickerGroup& group : window.groups) {
      const std::wstring label =
          group.name + L" " + std::to_wstring(group.tabs);
      SIZE size = {};
      GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()),
                            &size);
      if (x + Scale(14) + size.cx > rect.right - Scale(8)) {
        break;
      }
      const int dot = Scale(8);
      Dot(dc, x, middle + Scale(8), dot, group.color);
      text(label, small_font_, theme_.text_dim,
           {x + Scale(12), middle + Scale(2), x + Scale(12) + size.cx,
            middle + Scale(22)},
           DT_LEFT);
      x += Scale(12) + size.cx + Scale(14);
    }
  }
  // "Always…", the hint and the buttons.
  if (always_rect_.right > always_rect_.left) {
    const int box = Scale(14);
    RECT check = {always_rect_.left,
                  (always_rect_.top + always_rect_.bottom - box) / 2,
                  always_rect_.left + box,
                  (always_rect_.top + always_rect_.bottom + box) / 2};
    SetDCBrushColor(dc, always_ ? GetSysColor(COLOR_HIGHLIGHT) : theme_.field);
    FillRect(dc, &check, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetDCBrushColor(dc, theme_.text_dim);
    FrameRect(dc, &check, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    text(L"Always open links matching " + *rule_pattern_ +
             L" in this profile (A)",
         small_font_, theme_.text,
         {check.right + Scale(8), always_rect_.top, always_rect_.right,
          always_rect_.bottom},
         DT_LEFT | DT_END_ELLIPSIS);
  }
  const std::wstring hint = window_step_ && profiles_.size() > 1
                                ? L"1–9 or ↑↓ and Enter to choose · Esc or "
                                  L"← to go back"
                                : L"1–9 or ↑↓ and Enter to choose · Esc to "
                                  L"cancel";
  text(hint, small_font_, theme_.text_dim,
       {margin, cancel_rect_.top - Scale(22), right,
        cancel_rect_.top - Scale(2)},
       DT_LEFT | DT_END_ELLIPSIS);
  for (const auto& [rect, label] :
       {std::pair{back_rect_, L"Back"}, std::pair{cancel_rect_, L"Cancel"}}) {
    if (rect.right <= rect.left) {
      continue;
    }
    FillRectColor(dc, rect, theme_.field);
    SetDCBrushColor(dc, theme_.separator);
    FrameRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    text(label, text_font_, theme_.text, rect, DT_CENTER);
  }
  EndPaint(window_, &paint);
}

void LinkPicker::Select(int row) {
  if (row < 0 || row >= RowCount()) {
    return;
  }
  selected_ = row;
  if (selected_ < first_row_) {
    first_row_ = selected_;
  } else if (selected_ >= first_row_ + VisibleRows()) {
    first_row_ = selected_ - VisibleRows() + 1;
  }
  InvalidateRect(window_, nullptr, FALSE);
  UpdateThumbnails();
}

void LinkPicker::Choose(int row) {
  if (row < 0 || row >= RowCount()) {
    return;
  }
  if (!window_step_) {
    chosen_ = profiles_[static_cast<std::size_t>(row)].id;
    if (!ShowWindowStep()) {
      Finish(
          {.profile = chosen_, .always = always_ && rule_pattern_.has_value()});
    }
    return;
  }
  LinkPickerResult result{
      .profile = chosen_,
      .always = always_ && rule_pattern_.has_value() && profiles_.size() > 1};
  if (row == static_cast<int>(windows_.size())) {
    result.window = LinkPickerResult::Window::kNewWindow;
  } else {
    result.window = LinkPickerResult::Window::kExisting;
    result.window_id = windows_[static_cast<std::size_t>(row)].id;
  }
  Finish(result);
}

void LinkPicker::Back() {
  if (!window_step_ || profiles_.size() <= 1) {
    return;
  }
  int row = 0;
  for (std::size_t i = 0; i < profiles_.size(); ++i) {
    if (profiles_[i].id == chosen_) {
      row = static_cast<int>(i);
    }
  }
  ShowProfileStep(row);
}

void LinkPicker::Finish(const LinkPickerResult& result) {
  ClearThumbnails();
  if (window_) {
    ShowWindow(window_, SW_HIDE);
  }
  // May show the picker again (the next link): nothing here touches it
  // after.
  Done done = std::move(done_);
  done_ = nullptr;
  windows_for_ = nullptr;
  if (done) {
    done(result);
  }
}

LRESULT CALLBACK LinkPicker::WindowProc(HWND window,
                                        UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam) {
  auto* self =
      reinterpret_cast<LinkPicker*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT LinkPicker::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_ERASEBKGND:
      return 1;  // Painted in WM_PAINT.
    case WM_PAINT:
      Paint();
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTALLKEYS;
    case WM_KEYDOWN: {
      const int key = static_cast<int>(wparam);
      if (key >= '1' && key <= '9') {
        Choose(key - '1');
      } else if (key >= VK_NUMPAD1 && key <= VK_NUMPAD9) {
        Choose(key - VK_NUMPAD1);
      } else if (key == VK_UP) {
        Select(selected_ - 1);
      } else if (key == VK_DOWN) {
        Select(selected_ + 1);
      } else if (key == VK_RETURN) {
        Choose(selected_);
      } else if (key == VK_LEFT) {
        Back();
      } else if (key == VK_ESCAPE) {
        if (window_step_ && profiles_.size() > 1) {
          Back();
        } else {
          Finish(LinkPickerResult{.cancelled = true, .profile = {}});
        }
      } else if (key == 'A' && always_rect_.right > always_rect_.left) {
        always_ = !always_;
        InvalidateRect(window_, nullptr, FALSE);
      }
      return 0;
    }
    case WM_MOUSEMOVE:
      if (const int row = RowAt({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
          row >= 0 && row != selected_) {
        Select(row);
      }
      return 0;
    case WM_MOUSEWHEEL: {
      const int step = GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -1 : 1;
      first_row_ = std::clamp(first_row_ + step, 0,
                              std::max(0, RowCount() - VisibleRows()));
      InvalidateRect(window_, nullptr, FALSE);
      UpdateThumbnails();
      return 0;
    }
    case WM_LBUTTONUP: {
      const POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (PtInRect(&cancel_rect_, point)) {
        Finish(LinkPickerResult{.cancelled = true, .profile = {}});
      } else if (PtInRect(&back_rect_, point)) {
        Back();
      } else if (PtInRect(&always_rect_, point)) {
        always_ = !always_;
        InvalidateRect(window_, nullptr, FALSE);
      } else if (const int row = RowAt(point); row >= 0) {
        Choose(row);
      }
      return 0;
    }
    case WM_DPICHANGED:
      UpdateFonts();
      Layout();
      return 0;
    case WM_CLOSE:
      Finish(LinkPickerResult{.cancelled = true, .profile = {}});
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
