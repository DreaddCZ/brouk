#include "platform/win/downloads_popup.h"

#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <utility>

#include "core/download_record.h"
#include "platform/win/controls.h"
#include "platform/win/downloaded_files.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukDownloads";
constexpr int kWidth = 380;        // DIPs.
constexpr int kHeaderHeight = 40;  // "Downloads".
constexpr int kRowHeight = 52;     // Icon, name over status, buttons.
constexpr int kFooterHeight = 40;  // "Show all downloads".
constexpr int kButtonWidth = 30;
constexpr int kMaxRows = 6;
constexpr std::int64_t kRecentSeconds = 24 * 60 * 60;

// Menu command ids (TrackPopupMenu returns them).
enum MenuId : UINT_PTR {
  kMenuOpen = 1,
  kMenuReveal,
  kMenuCopyLink,
  kMenuRemove,
  kMenuDelete,
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

// The icon Windows shows for the file (or, without the file, for its type).
HICON FileIcon(const DownloadRecord& record, bool exists) {
  SHFILEINFOW info = {};
  std::wstring path = Utf8ToWide(exists ? record.path : record.FileName());
  UINT flags = SHGFI_ICON | SHGFI_LARGEICON;
  if (!exists) {
    flags |= SHGFI_USEFILEATTRIBUTES;
  }
  if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                     flags) == 0) {
    return nullptr;
  }
  return info.hIcon;
}

}  // namespace

DownloadsPopup::DownloadsPopup(Delegate& delegate) : delegate_(delegate) {}

// Glyphs of the "Segoe MDL2 Assets" icon font.
const wchar_t* DownloadsPopup::GlyphOf(Command command) {
  switch (command) {
    case Command::kPause:
      return L"\uE769";  // Pause.
    case Command::kResume:
      return L"\uE768";  // Play.
    case Command::kCancel:
      return L"\uE711";  // Cancel.
    case Command::kRetry:
      return L"\uE72C";  // Refresh.
    case Command::kReveal:
      return L"\uE838";  // FolderOpen.
    default:
      return L"\uE712";  // More.
  }
}

DownloadsPopup::~DownloadsPopup() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  ClearRows();
}

int DownloadsPopup::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(owner_)), 96);
}

std::vector<DownloadEntry> DownloadsPopup::Recent(
    std::vector<DownloadEntry> entries) {
  const std::int64_t now = std::time(nullptr);
  std::erase_if(entries, [now](const DownloadEntry& entry) {
    return !entry.record.Running() &&
           entry.record.start_time < now - kRecentSeconds;
  });
  return entries;
}

void DownloadsPopup::Show(HWND owner,
                          const RECT& anchor,
                          std::vector<DownloadEntry> entries,
                          const Theme& theme,
                          HFONT text_font,
                          HFONT icon_font) {
  if (!RegisterWindowClass()) {
    return;
  }
  owner_ = owner;
  anchor_ = anchor;
  theme_ = theme;
  text_font_ = text_font;
  icon_font_ = icon_font;
  if (!window_) {
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Downloads",
                              WS_POPUP | WS_BORDER, 0, 0, 0, 0, owner_, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (!window_) {
      return;
    }
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(window_, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(&DownloadsPopup::WindowProc));
  }
  scroll_ = 0;
  selected_ = -1;
  Fill(Recent(std::move(entries)));
  ShowWindow(window_, SW_SHOW);
  SetForegroundWindow(window_);
  SetFocus(window_);
}

void DownloadsPopup::Update(std::vector<DownloadEntry> entries) {
  if (visible()) {
    Fill(Recent(std::move(entries)));
  }
}

void DownloadsPopup::Close() {
  if (visible()) {
    ShowWindow(window_, SW_HIDE);
    if (owner_ && IsWindow(owner_)) {
      SetForegroundWindow(owner_);
    }
  }
}

void DownloadsPopup::ClearRows() {
  for (Row& row : rows_) {
    if (row.icon) {
      DestroyIcon(row.icon);
    }
  }
  rows_.clear();
}

void DownloadsPopup::Fill(std::vector<DownloadEntry> entries) {
  // Keeps the selection on the same download across updates.
  std::int64_t selected_id = -1;
  ProfileId selected_profile;
  if (selected_ >= 0 && static_cast<std::size_t>(selected_) < rows_.size()) {
    selected_id = rows_[static_cast<std::size_t>(selected_)].entry.record.id;
    selected_profile = rows_[static_cast<std::size_t>(selected_)].entry.profile;
  }
  ClearRows();
  selected_ = -1;
  using State = DownloadRecord::State;
  for (DownloadEntry& entry : entries) {
    Row row;
    const DownloadRecord& record = entry.record;
    const bool exists = DownloadedFileExists(record.path);
    row.missing = record.state == State::kComplete && !exists;
    row.name = Utf8ToWide(record.FileName());
    std::string status =
        row.missing ? std::string("Deleted") : DownloadStatusText(record);
    if (!record.referrer_site.empty()) {
      status = record.referrer_site + " \u00B7 " + status;
    }
    row.status = Utf8ToWide(status);
    row.icon = FileIcon(record, exists);
    switch (record.state) {
      case State::kInProgress:
        row.buttons = {Command::kPause, Command::kCancel};
        break;
      case State::kPaused:
        row.buttons = {Command::kResume, Command::kCancel};
        break;
      case State::kFailed:
      case State::kCancelled:
        row.buttons = {Command::kRetry};
        break;
      case State::kComplete:
        if (!row.missing) {
          row.buttons = {Command::kReveal};
        }
        break;
    }
    row.buttons.push_back(Command::kMore);
    if (record.id == selected_id && entry.profile == selected_profile) {
      selected_ = static_cast<int>(rows_.size());
    }
    row.entry = std::move(entry);
    rows_.push_back(std::move(row));
  }
  const int last_first = std::max(0, static_cast<int>(rows_.size()) - kMaxRows);
  scroll_ = std::clamp(scroll_, 0, last_first);
  Resize();
}

int DownloadsPopup::VisibleRows() const {
  // An empty list still has one row for "No downloads\u2026".
  return std::clamp(static_cast<int>(rows_.size()), 1, kMaxRows);
}

void DownloadsPopup::Resize() {
  const int width = Scale(kWidth);
  const int height = Scale(kHeaderHeight) + VisibleRows() * Scale(kRowHeight) +
                     Scale(kFooterHeight);
  int x = anchor_.right - width;
  int y = anchor_.bottom;
  // Kept on the button's monitor.
  MONITORINFO monitor = {.cbSize = sizeof(MONITORINFO),
                         .rcMonitor = {},
                         .rcWork = {},
                         .dwFlags = 0};
  if (GetMonitorInfoW(MonitorFromRect(&anchor_, MONITOR_DEFAULTTONEAREST),
                      &monitor)) {
    x = std::clamp(x, monitor.rcWork.left,
                   std::max(monitor.rcWork.left, monitor.rcWork.right - width));
    if (y + height > monitor.rcWork.bottom) {
      y = std::max(monitor.rcWork.top, anchor_.top - height);
    }
  }
  SetWindowPos(window_, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
  InvalidateRect(window_, nullptr, FALSE);
}

RECT DownloadsPopup::RowRect(int index) const {
  RECT client = {};
  GetClientRect(window_, &client);
  const int top = Scale(kHeaderHeight) + (index - scroll_) * Scale(kRowHeight);
  return {0, top, client.right, top + Scale(kRowHeight)};
}

RECT DownloadsPopup::FooterRect() const {
  RECT client = {};
  GetClientRect(window_, &client);
  return {0, client.bottom - Scale(kFooterHeight), client.right, client.bottom};
}

int DownloadsPopup::RowAt(POINT point, Command* button) const {
  *button = Command::kNone;
  const int top = Scale(kHeaderHeight);
  if (point.y < top) {
    return -1;
  }
  const int index = scroll_ + (point.y - top) / Scale(kRowHeight);
  if (index - scroll_ >= VisibleRows() ||
      static_cast<std::size_t>(index) >= rows_.size()) {
    return -1;
  }
  const Row& row = rows_[static_cast<std::size_t>(index)];
  const RECT area = RowRect(index);
  const int buttons_left =
      area.right - Scale(8) -
      static_cast<int>(row.buttons.size()) * Scale(kButtonWidth);
  if (point.x >= buttons_left && point.x < area.right - Scale(8)) {
    const auto slot = static_cast<std::size_t>((point.x - buttons_left) /
                                               Scale(kButtonWidth));
    if (slot < row.buttons.size()) {
      *button = row.buttons[slot];
    }
  }
  return index;
}

void DownloadsPopup::Select(int index) {
  if (rows_.empty()) {
    return;
  }
  selected_ = std::clamp(index, 0, static_cast<int>(rows_.size()) - 1);
  // Scrolls the selection into view.
  if (selected_ < scroll_) {
    scroll_ = selected_;
  } else if (selected_ >= scroll_ + kMaxRows) {
    scroll_ = selected_ - kMaxRows + 1;
  }
  InvalidateRect(window_, nullptr, FALSE);
}

void DownloadsPopup::Run(int index, Command command) {
  if (index < 0 || static_cast<std::size_t>(index) >= rows_.size()) {
    return;
  }
  // A copy: the actions below may refill rows_.
  const DownloadEntry entry = rows_[static_cast<std::size_t>(index)].entry;
  const DownloadRecord& record = entry.record;
  DownloadService* service = delegate_.PopupDownloads();
  switch (command) {
    case Command::kOpen:
      if (record.state == DownloadRecord::State::kComplete &&
          DownloadedFileExists(record.path)) {
        Close();
        OpenDownloadedFile(record.path);
      }
      break;
    case Command::kReveal:
      RevealDownloadedFile(record.path);
      break;
    case Command::kPause:
      if (service) {
        service->Pause(entry.profile, record.id);
      }
      break;
    case Command::kResume:
      if (service) {
        service->Resume(entry.profile, record.id);
      }
      break;
    case Command::kCancel:
      if (service) {
        service->Cancel(entry.profile, record.id);
      }
      break;
    case Command::kRetry:
      delegate_.PopupRetryDownload(entry);
      break;
    case Command::kMore: {
      RECT area = RowRect(index);
      POINT corner = {area.right - Scale(8), area.bottom};
      ClientToScreen(window_, &corner);
      ShowMenu(index, corner);
      break;
    }
    case Command::kCopyLink:
      SetClipboardText(window_, Utf8ToWide(record.url));
      break;
    case Command::kDelete:
      RecycleDownloadedFile(record.path);
      [[fallthrough]];
    case Command::kRemove:
      if (service) {
        service->Remove(entry.profile, record.id);
      }
      // Gone at once; the update from the service follows.
      if (static_cast<std::size_t>(index) < rows_.size()) {
        if (rows_[static_cast<std::size_t>(index)].icon) {
          DestroyIcon(rows_[static_cast<std::size_t>(index)].icon);
        }
        rows_.erase(rows_.begin() + index);
        selected_ =
            rows_.empty()
                ? -1
                : std::min(selected_, static_cast<int>(rows_.size()) - 1);
        scroll_ = std::clamp(
            scroll_, 0, std::max(0, static_cast<int>(rows_.size()) - kMaxRows));
        Resize();
      }
      break;
    case Command::kNone:
      break;
  }
}

void DownloadsPopup::ShowMenu(int index, POINT screen) {
  const Row& row = rows_[static_cast<std::size_t>(index)];
  const bool complete =
      row.entry.record.state == DownloadRecord::State::kComplete &&
      !row.missing;
  HMENU menu = CreatePopupMenu();
  if (complete) {
    AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open");
    AppendMenuW(menu, MF_STRING, kMenuReveal, L"Show in folder");
  }
  AppendMenuW(menu, MF_STRING, kMenuCopyLink, L"Copy download link");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuRemove, L"Remove from list");
  if (complete) {
    AppendMenuW(menu, MF_STRING, kMenuDelete, L"Delete");
  }
  const UINT chosen = static_cast<UINT>(
      TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN,
                     screen.x, screen.y, 0, window_, nullptr));
  DestroyMenu(menu);
  switch (chosen) {
    case kMenuOpen:
      Run(index, Command::kOpen);
      break;
    case kMenuReveal:
      Run(index, Command::kReveal);
      break;
    case kMenuCopyLink:
      Run(index, Command::kCopyLink);
      break;
    case kMenuRemove:
      Run(index, Command::kRemove);
      break;
    case kMenuDelete:
      Run(index, Command::kDelete);
      break;
    default:
      break;
  }
}

void DownloadsPopup::DragFile(int index) {
  const DownloadRecord& record =
      rows_[static_cast<std::size_t>(index)].entry.record;
  if (record.state != DownloadRecord::State::kComplete ||
      !DownloadedFileExists(record.path)) {
    return;
  }
  std::wstring path = Utf8ToWide(record.path);
  std::ranges::replace(path, L'/', L'\\');
  Microsoft::WRL::ComPtr<IShellItem> item;
  Microsoft::WRL::ComPtr<IDataObject> data;
  if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr,
                                         IID_PPV_ARGS(&item))) ||
      FAILED(
          item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data)))) {
    return;
  }
  // The shell's own drag source: the file is copied (or linked) where it
  // is dropped, as from File Explorer.
  DWORD effect = DROPEFFECT_NONE;
  SHDoDragDrop(window_, data.Get(), nullptr, DROPEFFECT_COPY | DROPEFFECT_LINK,
               &effect);
}

void DownloadsPopup::PaintRow(HDC dc,
                              int index,
                              const RECT& area,
                              bool selected) {
  const Row& row = rows_[static_cast<std::size_t>(index)];
  if (selected) {
    SetDCBrushColor(dc, theme_.hover);
    FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  }
  const int middle = (area.top + area.bottom) / 2;
  // The profile's colour.
  const int dot = Scale(6);
  const COLORREF color = delegate_.PopupProfileColor(row.entry.profile);
  SetDCBrushColor(dc, color);
  SetDCPenColor(dc, color);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
  HGDIOBJ old_pen = SelectObject(dc, GetStockObject(DC_PEN));
  Ellipse(dc, area.left + Scale(8), middle - dot / 2,
          area.left + Scale(8) + dot, middle + dot / 2 + dot % 2);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  const int icon = Scale(32);
  const int icon_left = area.left + Scale(20);
  if (row.icon) {
    DrawIconEx(dc, icon_left, middle - icon / 2, row.icon, icon, icon, 0,
               nullptr, DI_NORMAL);
  }
  const int buttons_left =
      area.right - Scale(8) -
      static_cast<int>(row.buttons.size()) * Scale(kButtonWidth);
  const int text_left = icon_left + icon + Scale(10);
  SelectObject(dc, text_font_);
  RECT name = {text_left, area.top + Scale(6), buttons_left - Scale(4),
               middle + Scale(1)};
  SetTextColor(dc, row.missing ? theme_.text_dim : theme_.text);
  DrawTextW(dc, row.name.c_str(), -1, &name,
            DT_SINGLELINE | DT_BOTTOM | DT_NOPREFIX | DT_PATH_ELLIPSIS);
  RECT status = {text_left, middle + Scale(3), buttons_left - Scale(4),
                 area.bottom - Scale(6)};
  const bool failed = row.entry.record.state == DownloadRecord::State::kFailed;
  SetTextColor(dc, failed
                       ? (theme_.dark ? RGB(255, 153, 164) : RGB(196, 43, 28))
                       : theme_.text_dim);
  DrawTextW(dc, row.status.c_str(), -1, &status,
            DT_SINGLELINE | DT_TOP | DT_NOPREFIX | DT_END_ELLIPSIS);
  SelectObject(dc, icon_font_);
  for (std::size_t i = 0; i < row.buttons.size(); ++i) {
    const Command command = row.buttons[i];
    RECT button = {buttons_left + static_cast<int>(i) * Scale(kButtonWidth),
                   middle - Scale(14),
                   buttons_left + static_cast<int>(i + 1) * Scale(kButtonWidth),
                   middle + Scale(14)};
    if (index == hovered_ && command == hovered_button_) {
      SetDCBrushColor(dc, theme_.separator);
      FillRect(dc, &button, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    SetTextColor(dc, theme_.text);
    DrawTextW(dc, GlyphOf(command), -1, &button,
              DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
  }
}

void DownloadsPopup::Paint() {
  PAINTSTRUCT paint = {};
  HDC window_dc = BeginPaint(window_, &paint);
  RECT client = {};
  GetClientRect(window_, &client);
  // Drawn off screen, so updates every second do not flicker.
  HDC dc = CreateCompatibleDC(window_dc);
  HBITMAP bitmap =
      CreateCompatibleBitmap(window_dc, client.right, client.bottom);
  HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
  HGDIOBJ old_font = SelectObject(dc, text_font_);
  SetDCBrushColor(dc, theme_.toolbar);
  FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  SetBkMode(dc, TRANSPARENT);
  const int margin = Scale(14);
  RECT header = {margin, 0, client.right - margin, Scale(kHeaderHeight)};
  SetTextColor(dc, theme_.text);
  DrawTextW(dc, L"Downloads", -1, &header,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  if (rows_.empty()) {
    RECT empty = RowRect(0);
    empty.left += margin;
    SetTextColor(dc, theme_.text_dim);
    DrawTextW(dc, L"No downloads in the last 24 hours.", -1, &empty,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  }
  const int last = std::min(static_cast<int>(rows_.size()), scroll_ + kMaxRows);
  for (int i = scroll_; i < last; ++i) {
    PaintRow(dc, i, RowRect(i),
             i == selected_ || (i == hovered_ && selected_ < 0));
  }
  const RECT footer = FooterRect();
  RECT line = {footer.left, footer.top, footer.right, footer.top + 1};
  SetDCBrushColor(dc, theme_.separator);
  FillRect(dc, &line, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  if (footer_hovered_) {
    RECT hover = {footer.left, footer.top + 1, footer.right, footer.bottom};
    SetDCBrushColor(dc, theme_.hover);
    FillRect(dc, &hover, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  }
  RECT footer_text = {margin, footer.top, footer.right - margin, footer.bottom};
  SelectObject(dc, text_font_);
  SetTextColor(dc, theme_.text);
  DrawTextW(dc, L"Show all downloads", -1, &footer_text,
            DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  BitBlt(window_dc, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
  SelectObject(dc, old_font);
  SelectObject(dc, old_bitmap);
  DeleteObject(bitmap);
  DeleteDC(dc);
  EndPaint(window_, &paint);
}

LRESULT CALLBACK DownloadsPopup::WindowProc(HWND window,
                                            UINT message,
                                            WPARAM wparam,
                                            LPARAM lparam) {
  auto* self = reinterpret_cast<DownloadsPopup*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT DownloadsPopup::HandleMessage(UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam) {
  switch (message) {
    case WM_ACTIVATE:
      // Clicking elsewhere closes it (not its own menu, which keeps the
      // window active).
      if (LOWORD(wparam) == WA_INACTIVE && visible()) {
        ShowWindow(window_, SW_HIDE);
        hidden_at_ = GetTickCount64();
      }
      break;
    case WM_ERASEBKGND:
      return 1;  // Painted in WM_PAINT.
    case WM_PAINT:
      Paint();
      return 0;
    case WM_GETDLGCODE:
      return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
      switch (wparam) {
        case VK_DOWN:
          Select(selected_ + 1);
          return 0;
        case VK_UP:
          Select(selected_ < 0 ? 0 : selected_ - 1);
          return 0;
        case VK_RETURN:
          if (selected_ >= 0) {
            Run(selected_, Command::kOpen);
          } else {
            Close();
            delegate_.PopupShowAllDownloads();
          }
          return 0;
        case VK_DELETE:
          Run(selected_, Command::kRemove);
          return 0;
        case VK_ESCAPE:
          Close();
          return 0;
        case VK_APPS:
          Run(selected_, Command::kMore);
          return 0;
        case VK_F10:
          if (KeyDown(VK_SHIFT)) {
            Run(selected_, Command::kMore);
            return 0;
          }
          break;
        default:
          break;
      }
      break;
    case WM_SYSKEYDOWN:
      // Shift+F10 arrives as a system key.
      if (wparam == VK_F10 && KeyDown(VK_SHIFT)) {
        Run(selected_, Command::kMore);
        return 0;
      }
      break;
    case WM_LBUTTONDOWN: {
      Command button = Command::kNone;
      press_point_ = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      pressed_ = RowAt(press_point_, &button);
      return 0;
    }
    case WM_MOUSEMOVE: {
      const POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      if (pressed_ >= 0 && (wparam & MK_LBUTTON) != 0 &&
          (std::abs(point.x - press_point_.x) > GetSystemMetrics(SM_CXDRAG) ||
           std::abs(point.y - press_point_.y) > GetSystemMetrics(SM_CYDRAG))) {
        const int dragged = pressed_;
        pressed_ = -1;
        DragFile(dragged);
        return 0;
      }
      Command button = Command::kNone;
      const int row = RowAt(point, &button);
      const RECT footer = FooterRect();
      const bool in_footer = PtInRect(&footer, point) != FALSE;
      if (row != hovered_ || button != hovered_button_ ||
          in_footer != footer_hovered_) {
        hovered_ = row;
        hovered_button_ = button;
        footer_hovered_ = in_footer;
        if (row >= 0) {
          selected_ = -1;  // The mouse takes over from the keyboard.
        }
        InvalidateRect(window_, nullptr, FALSE);
        TRACKMOUSEEVENT track = {.cbSize = sizeof(TRACKMOUSEEVENT),
                                 .dwFlags = TME_LEAVE,
                                 .hwndTrack = window_,
                                 .dwHoverTime = 0};
        TrackMouseEvent(&track);
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      hovered_ = -1;
      hovered_button_ = Command::kNone;
      footer_hovered_ = false;
      InvalidateRect(window_, nullptr, FALSE);
      return 0;
    case WM_MOUSEWHEEL: {
      const int notches = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
      const int last_first =
          std::max(0, static_cast<int>(rows_.size()) - kMaxRows);
      scroll_ = std::clamp(scroll_ - notches, 0, last_first);
      InvalidateRect(window_, nullptr, FALSE);
      return 0;
    }
    case WM_LBUTTONUP: {
      const POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      RECT footer = FooterRect();
      if (PtInRect(&footer, point)) {
        Close();
        delegate_.PopupShowAllDownloads();
        return 0;
      }
      Command button = Command::kNone;
      const int row = RowAt(point, &button);
      // Only a click that started on the row (not the end of a drag).
      if (row >= 0 && row == pressed_) {
        Run(row, button == Command::kNone ? Command::kOpen : button);
      }
      pressed_ = -1;
      return 0;
    }
    case WM_RBUTTONUP: {
      const POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      Command button = Command::kNone;
      if (const int row = RowAt(point, &button); row >= 0) {
        POINT screen = point;
        ClientToScreen(window_, &screen);
        ShowMenu(row, screen);
      }
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
