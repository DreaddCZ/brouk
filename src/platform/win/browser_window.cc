#include "platform/win/browser_window.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "brouk/app_info.h"
#include "cef/function_task.h"
#include "cef/site_info.h"
#include "core/address_input.h"
#include "core/internal_pages.h"
#include "core/profile_id.h"
#include "core/zoom.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/controls.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukBrowserWindow";

// Layout in device-independent pixels (96 DPI).
constexpr int kStripHeight = 40;
constexpr int kToolbarHeight = 40;
constexpr int kButtonSize = 32;
constexpr int kMargin = 4;
constexpr int kAddressHeight = 30;
constexpr int kAddressPadding = 12;

constexpr UINT_PTR kSpinnerTimer = 1;
constexpr UINT kSpinnerInterval = 80;  // Milliseconds per step.
constexpr UINT_PTR kFullscreenHintTimer = 2;
constexpr UINT kFullscreenHintTime = 3000;  // Milliseconds.

enum ControlId : int {
  kBackId = 1,
  kForwardId,
  kReloadId,
  kMenuId,
  kAddressId,
  kZoomId,
  kPopupsId,
  kDownloadsId,
};

// Items of the "⋮" menu.
enum MenuCommand : UINT {
  kNewTabCommand = 100,
  kNewWindowCommand,
  kZoomInCommand,
  kZoomOutCommand,
  kZoomResetCommand,
  kPrintCommand,
  kFindCommand,
  kHelpCommand,
  kSettingsCommand,
  kLicencesCommand,
  kExitCommand,
  kAllowPopupsCommand,
  kManageProfilesCommand,
  kNewTabToTheRightCommand,
  kReloadTabCommand,
  kDuplicateTabCommand,
  kPinTabCommand,
  kMuteTabCommand,
  kCloseTabCommand,
  kCloseOtherTabsCommand,
  kCloseTabsLeftCommand,
  kCloseTabsRightCommand,
  kReopenClosedTabCommand,
  kHistoryCommand,
  kClearBrowsingDataCommand,
  kDownloadsCommand,
  kFirstBlockedPopupCommand = 1000,
  kFirstProfileCommand = 2000,
  kFirstClosedCommand = 3000,
  kFirstRecentPageCommand = 4000,
};

// Glyphs of the "Segoe MDL2 Assets" icon font, on Windows 10 and 11.
constexpr wchar_t kBackGlyph[] = L"\uE72B";
constexpr wchar_t kForwardGlyph[] = L"\uE72A";
constexpr wchar_t kReloadGlyph[] = L"\uE72C";
constexpr wchar_t kMoreGlyph[] = L"\uE712";
constexpr wchar_t kBlockedGlyph[] = L"\uE8A7";  // OpenInNewWindow.
constexpr wchar_t kDownloadGlyph[] = L"\uE896";

std::int64_t NextWindowId() {
  static std::int64_t next = 1;
  return next++;
}

// Menu text shows "&" as a mnemonic unless doubled.
std::wstring MenuText(const std::string& text) {
  std::wstring escaped;
  for (wchar_t c : Utf8ToWide(text)) {
    escaped += c;
    if (c == L'&') {
      escaped += L'&';
    }
  }
  return escaped;
}

TabId NextTabId() {
  static TabId next = 0;
  return ++next;
}

bool RegisterWindowClass() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;  // Replaced per window.
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

// Whether `window` itself is shown (IsWindowVisible also needs its
// parents to be).
bool HasVisibleStyle(HWND window) {
  return (GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) != 0;
}

bool IsBlank(const std::string& url) {
  return url.empty() || url == "about:blank";
}

}  // namespace

BrowserWindow::BrowserWindow(BrowserWindowOwner& owner, Kind kind)
    : owner_(owner),
      kind_(kind),
      window_id_(NextWindowId()),
      model_(owner.DefaultProfile()) {}

BrowserWindow::~BrowserWindow() {
  fullscreen_hint_.reset();
  tabs_.clear();
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  for (HFONT font : {text_font_, icon_font_}) {
    if (font) {
      DeleteObject(font);
    }
  }
  if (field_brush_) {
    DeleteObject(field_brush_);
  }
}

bool BrowserWindow::Create(int content_width, int content_height) {
  CEF_REQUIRE_UI_THREAD();
  if (!RegisterWindowClass()) {
    return false;
  }
  const int dpi = static_cast<int>(GetDpiForSystem());
  int width = MulDiv(1200, dpi, 96);
  int height = MulDiv(800, dpi, 96);
  if (kind_ == Kind::kPopup && content_width > 0 && content_height > 0) {
    // The page area gets the size the page asked for.
    RECT rect = {0, 0, content_width,
                 content_height + MulDiv(kToolbarHeight, dpi, 96)};
    AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0,
                             static_cast<UINT>(dpi));
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
  }
  window_ = CreateWindowExW(
      0, kWindowClass, Utf8ToWide(kAppName).c_str(),
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
      width, height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    return false;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(window_, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(&BrowserWindow::WindowProc));
  theme_ = CurrentTheme();
  ApplyThemeToFrame(window_, theme_);
  CreateControls();
  UpdateFonts();
  // Recomputes the frame with our WM_NCCALCSIZE (no system title bar).
  SetWindowPos(window_, nullptr, 0, 0, 0, 0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOACTIVATE);
  Layout();
  return true;
}

void BrowserWindow::Show() {
  ShowWindow(window_, show_command_);
  FocusActiveTab();
}

void BrowserWindow::Activate() {
  if (IsIconic(window_)) {
    ShowWindow(window_, SW_RESTORE);
  }
  SetForegroundWindow(window_);
}

void BrowserWindow::CreateControls() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  auto button = [&](const wchar_t* glyph, ControlId id, const wchar_t* name) {
    HWND control = CreateWindowExW(
        0, WC_BUTTONW, glyph, WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SetAccessibleName(control, name);
    if (kind_ == Kind::kNormal) {
      ShowWindow(control, SW_SHOWNA);
    }
    return control;
  };
  back_ = button(kBackGlyph, kBackId, L"Back");
  forward_ = button(kForwardGlyph, kForwardId, L"Forward");
  reload_ = button(kReloadGlyph, kReloadId, L"Reload");
  menu_ = button(kMoreGlyph, kMenuId, L"Customise and control Brouk");
  // Shown once something was downloaded in this run (Chrome).
  downloads_ = CreateWindowExW(
      0, WC_BUTTONW, kDownloadGlyph, WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0,
      window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDownloadsId)),
      instance, nullptr);
  SetAccessibleName(downloads_, L"Downloads");
  DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
  if (kind_ == Kind::kPopup) {
    style |= ES_READONLY;  // Popups show where they are (Chrome).
  }
  address_ =
      CreateWindowExW(0, WC_EDITW, L"", style, 0, 0, 0, 0, window_,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddressId)),
                      instance, nullptr);
  SendMessageW(address_, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(L"Search or type a web address"));
  SetAccessibleName(address_, L"Address and search bar");
  SetWindowSubclass(address_, &BrowserWindow::AddressProc, 0,
                    reinterpret_cast<DWORD_PTR>(this));
  zoom_ = CreateWindowExW(
      0, WC_BUTTONW, L"", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kZoomId)), instance,
      nullptr);
  popups_ = CreateWindowExW(
      0, WC_BUTTONW, kBlockedGlyph, WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0,
      window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPopupsId)),
      instance, nullptr);
  SetAccessibleName(popups_, L"Pop-ups blocked");
  if (kind_ == Kind::kNormal) {
    // One tool over the tab strip; its text follows the hovered part.
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0,
                               0, window_, nullptr, instance, nullptr);
    TTTOOLINFOW tool = {};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_SUBCLASS;
    tool.hwnd = window_;
    tool.uId = 1;
    tool.lpszText = LPSTR_TEXTCALLBACKW;
    SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
  }
}

int BrowserWindow::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(window_)), 96);
}

int BrowserWindow::FaviconSize() {
  return Scale(16);
}

void BrowserWindow::UpdateFonts() {
  const UINT dpi = GetDpiForWindow(window_);
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                             0, dpi);
  HFONT text_font = CreateFontIndirectW(&metrics.lfMessageFont);
  LOGFONTW icon = {};
  icon.lfHeight = -MulDiv(16, static_cast<int>(dpi), 96);
  icon.lfWeight = FW_NORMAL;
  icon.lfCharSet = DEFAULT_CHARSET;
  icon.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(icon.lfFaceName, L"Segoe MDL2 Assets");
  HFONT icon_font = CreateFontIndirectW(&icon);
  SendMessageW(address_, WM_SETFONT, reinterpret_cast<WPARAM>(text_font), TRUE);
  for (HFONT font : {text_font_, icon_font_}) {
    if (font) {
      DeleteObject(font);
    }
  }
  text_font_ = text_font;
  icon_font_ = icon_font;

  HDC dc = GetDC(window_);
  HGDIOBJ previous = SelectObject(dc, text_font_);
  TEXTMETRICW text_metrics = {};
  GetTextMetricsW(dc, &text_metrics);
  SelectObject(dc, previous);
  ReleaseDC(window_, dc);
  text_height_ = text_metrics.tmHeight;

  if (field_brush_) {
    DeleteObject(field_brush_);
  }
  field_brush_ = CreateSolidBrush(theme_.field);
}

int BrowserWindow::ResizeBorderHeight() const {
  const UINT dpi = GetDpiForWindow(window_);
  return GetSystemMetricsForDpi(SM_CYFRAME, dpi) +
         GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

RECT BrowserWindow::StripBounds() const {
  RECT client = {};
  GetClientRect(window_, &client);
  if (kind_ == Kind::kPopup || fullscreen_) {
    return {0, 0, client.right, 0};
  }
  return {0, 0, client.right,
          std::min(static_cast<int>(client.bottom), Scale(kStripHeight))};
}

RECT BrowserWindow::ToolbarBounds() const {
  RECT client = {};
  GetClientRect(window_, &client);
  const RECT strip = StripBounds();
  if (fullscreen_) {
    return {0, 0, client.right, 0};
  }
  return {0, strip.bottom, client.right,
          std::min(static_cast<int>(client.bottom),
                   static_cast<int>(strip.bottom) + Scale(kToolbarHeight))};
}

RECT BrowserWindow::ContentBounds() const {
  RECT client = {};
  GetClientRect(window_, &client);
  client.top = ToolbarBounds().bottom;
  return client;
}

void BrowserWindow::Layout() {
  LayoutStrip();
  if (tooltip_) {
    TTTOOLINFOW tool = {};
    tool.cbSize = sizeof(tool);
    tool.hwnd = window_;
    tool.uId = 1;
    tool.rect = StripBounds();
    SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0,
                 reinterpret_cast<LPARAM>(&tool));
  }
  const RECT toolbar = ToolbarBounds();
  const int toolbar_height = toolbar.bottom - toolbar.top;
  const int button = Scale(kButtonSize);
  const int margin = Scale(kMargin);
  int x = margin;
  if (kind_ == Kind::kNormal) {
    for (HWND control : {back_, forward_, reload_}) {
      SetWindowPos(control, nullptr, x,
                   toolbar.top + (toolbar_height - button) / 2, button, button,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      x += button + margin;
    }
  }
  int right = static_cast<int>(toolbar.right) - margin;
  if (kind_ == Kind::kNormal) {
    right -= button;
    SetWindowPos(menu_, nullptr, right,
                 toolbar.top + (toolbar_height - button) / 2, button, button,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    right -= margin;
    if (HasVisibleStyle(downloads_)) {
      right -= button;
      SetWindowPos(downloads_, nullptr, right,
                   toolbar.top + (toolbar_height - button) / 2, button, button,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      right -= margin;
    }
  }
  const int field_height = Scale(kAddressHeight);
  const int field_top = toolbar.top + (toolbar_height - field_height) / 2;
  address_field_ = {x, field_top, std::max(x, right - margin),
                    field_top + field_height};
  const int padding = Scale(kAddressPadding);
  const int edit_height = text_height_ + Scale(2);
  // The indicators at the right end of the address bar, right to left.
  int indicator_right = static_cast<int>(address_field_.right) - Scale(4);
  for (const auto& [indicator, width] :
       {std::pair{zoom_, Scale(56)}, std::pair{popups_, Scale(30)}}) {
    if (HasVisibleStyle(indicator)) {
      indicator_right -= width;
      SetWindowPos(indicator, nullptr, indicator_right, field_top + Scale(4),
                   width, field_height - Scale(8),
                   SWP_NOZORDER | SWP_NOACTIVATE);
      indicator_right -= Scale(4);
    }
  }
  const int edit_right = std::min(
      static_cast<int>(address_field_.right) - padding, indicator_right);
  SetWindowPos(
      address_, nullptr, address_field_.left + padding,
      field_top + (field_height - edit_height) / 2,
      std::max(0, edit_right - static_cast<int>(address_field_.left) - padding),
      edit_height, SWP_NOZORDER | SWP_NOACTIVATE);
  const RECT content = ContentBounds();
  for (const auto& [id, tab] : tabs_) {
    tab->SetBounds(content);
  }
  InvalidateRect(window_, nullptr, FALSE);
}

void BrowserWindow::InvalidateStrip() {
  const RECT strip = StripBounds();
  InvalidateRect(window_, &strip, FALSE);
}

void BrowserWindow::Paint() {
  PAINTSTRUCT paint;
  HDC dc = BeginPaint(window_, &paint);
  RECT client = {};
  GetClientRect(window_, &client);
  const RECT toolbar = ToolbarBounds();
  // Drawn off screen first, so the title bar does not flicker.
  const int width = client.right;
  const int height = toolbar.bottom;
  if (width > 0 && height > 0) {
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
    HGDIOBJ previous = SelectObject(buffer, bitmap);
    if (kind_ == Kind::kNormal) {
      TabStrip::PaintState state;
      state.theme = &theme_;
      state.tab = [this](TabId id) -> const Tab* { return FindTab(id); };
      state.active = model_.active_id();
      state.hover = hover_;
      state.pressed = pressed_;
      state.window_active = active_;
      state.maximized = IsZoomed(window_) != FALSE;
      state.dark = theme_.dark;
      state.spinner_step = spinner_step_;
      strip_.Paint(buffer, state);
    }
    PaintToolbar(buffer);
    BitBlt(dc, 0, 0, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previous);
    DeleteObject(bitmap);
    DeleteDC(buffer);
  }
  // The page area, while a tab has no browser yet.
  RECT content = ContentBounds();
  SetDCBrushColor(dc, theme_.toolbar);
  FillRect(dc, &content, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  EndPaint(window_, &paint);
}

void BrowserWindow::PaintToolbar(HDC dc) {
  const RECT toolbar = ToolbarBounds();
  SetDCBrushColor(dc, theme_.toolbar);
  FillRect(dc, &toolbar, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  // The address bar as a rounded field around the edit control.
  SetDCBrushColor(dc, theme_.field);
  SetDCPenColor(dc, theme_.field);
  HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
  HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
  const int radius = address_field_.bottom - address_field_.top;
  RoundRect(dc, address_field_.left, address_field_.top, address_field_.right,
            address_field_.bottom, radius, radius);
  SelectObject(dc, brush);
  SelectObject(dc, pen);
}

void BrowserWindow::DrawToolbarButton(const DRAWITEMSTRUCT& item) {
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  const bool disabled = (item.itemState & ODS_DISABLED) != 0;
  SetDCBrushColor(item.hDC, pressed ? theme_.hover : theme_.toolbar);
  FillRect(item.hDC, &item.rcItem,
           static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  wchar_t glyph[4] = {};
  GetWindowTextW(item.hwndItem, glyph, 4);
  HGDIOBJ previous = SelectObject(item.hDC, icon_font_);
  SetBkMode(item.hDC, TRANSPARENT);
  SetTextColor(item.hDC, disabled ? theme_.separator : theme_.text);
  RECT area = item.rcItem;
  DrawTextW(item.hDC, glyph, -1, &area,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(item.hDC, previous);
}

void BrowserWindow::DrawDownloadsButton(const DRAWITEMSTRUCT& item) {
  DrawToolbarButton(item);
  DownloadService* service = owner_.Downloads();
  if (!service) {
    return;
  }
  const RECT& area = item.rcItem;
  const int width = area.right - area.left;
  if (service->RunningCount() > 0) {
    // A line under the glyph: how far the running downloads are (a short
    // one when their size is unknown).
    const double progress = service->Progress();
    const int left = area.left + width / 4;
    const int full = width / 2;
    const int done =
        progress < 0 ? full / 4
                     : static_cast<int>(full * std::clamp(progress, 0.0, 1.0));
    const int top = area.bottom - Scale(6);
    RECT track = {left, top, left + full, top + Scale(2)};
    SetDCBrushColor(item.hDC, theme_.separator);
    FillRect(item.hDC, &track, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    RECT bar = {left, top, left + std::max(done, Scale(2)), top + Scale(2)};
    SetDCBrushColor(item.hDC, GetSysColor(COLOR_HIGHLIGHT));
    FillRect(item.hDC, &bar, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  } else if (service->HasUnseen()) {
    const int size = Scale(6);
    const int x = area.right - Scale(9);
    const int y = area.top + Scale(5);
    SetDCBrushColor(item.hDC, GetSysColor(COLOR_HIGHLIGHT));
    SetDCPenColor(item.hDC, GetSysColor(COLOR_HIGHLIGHT));
    HGDIOBJ old_brush = SelectObject(item.hDC, GetStockObject(DC_BRUSH));
    HGDIOBJ old_pen = SelectObject(item.hDC, GetStockObject(DC_PEN));
    Ellipse(item.hDC, x, y, x + size, y + size);
    SelectObject(item.hDC, old_pen);
    SelectObject(item.hDC, old_brush);
  }
}

void BrowserWindow::DownloadsChanged() {
  DownloadService* service = owner_.Downloads();
  const bool show = kind_ == Kind::kNormal && service && service->AnyThisRun();
  if (show != HasVisibleStyle(downloads_)) {
    ShowWindow(downloads_, show ? SW_SHOWNA : SW_HIDE);
    Layout();
  }
  InvalidateRect(downloads_, nullptr, FALSE);
}

void BrowserWindow::ShowDownloads() {
  if (DownloadService* service = owner_.Downloads()) {
    service->MarkAllSeen();
  }
  OpenInternalPage("brouk://downloads/");
}

void BrowserWindow::RetryDownload(const DownloadEntry& entry) {
  DownloadService* service = owner_.Downloads();
  CefRefPtr<CefBrowser> browser = AnyBrowserInProfile(entry.profile);
  if (browser && service) {
    service->Retry(entry.profile, entry.record.id, entry.record.url, browser);
    return;
  }
  // No tab of the profile here: the address in a new tab starts it again.
  OpenTab(entry.record.url, false, entry.profile);
}

DownloadService* BrowserWindow::TabDownloads() {
  return owner_.Downloads();
}

bool BrowserWindow::TabAskWhereToSaveDownloads() {
  return owner_.AskWhereToSaveDownloads();
}

DownloadTarget BrowserWindow::TabReserveDownloadPath(
    const std::string& suggested,
    const ProfileId& profile) {
  return owner_.ReserveDownloadPath(suggested, profile);
}

void BrowserWindow::TabReleaseDownloadPath(const std::string& path) {
  owner_.ReleaseDownloadPath(path);
}

void BrowserWindow::OnTabFullscreenChanged(Tab& tab, bool fullscreen) {
  if (fullscreen) {
    if (model_.active_id() != tab.id()) {
      tab.ExitFullscreen();
      return;
    }
    fullscreen_tab_ = tab.id();
    if (!fullscreen_) {
      fullscreen_for_page_ = true;
      SetFullscreen(true);
    }
    ShowFullscreenHint(L"Press Esc to exit full screen");
  } else if (fullscreen_tab_ == tab.id()) {
    fullscreen_tab_.reset();
    if (fullscreen_for_page_) {
      fullscreen_for_page_ = false;
      SetFullscreen(false);
    }
  }
}

void BrowserWindow::SetFullscreen(bool fullscreen) {
  if (fullscreen == fullscreen_) {
    return;
  }
  if (fullscreen) {
    saved_placement_ = {};
    saved_placement_.length = sizeof(saved_placement_);
    GetWindowPlacement(window_, &saved_placement_);
    saved_style_ = GetWindowLongPtrW(window_, GWL_STYLE);
    fullscreen_ = true;
    SetWindowLongPtrW(
        window_, GWL_STYLE,
        saved_style_ & ~static_cast<LONG_PTR>(WS_CAPTION | WS_THICKFRAME));
    MONITORINFO monitor = {};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST),
                    &monitor);
    const RECT& area = monitor.rcMonitor;
    SetWindowPos(window_, HWND_TOP, area.left, area.top, area.right - area.left,
                 area.bottom - area.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
  } else {
    fullscreen_ = false;
    fullscreen_hint_.reset();
    SetWindowLongPtrW(window_, GWL_STYLE, saved_style_);
    SetWindowPlacement(window_, &saved_placement_);
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                     SWP_FRAMECHANGED);
  }
  // The toolbar's controls; the tab strip is painted and hit-tested only
  // outside full screen.
  for (HWND control : {back_, forward_, reload_, menu_}) {
    if (kind_ == Kind::kNormal) {
      ShowWindow(control, fullscreen ? SW_HIDE : SW_SHOWNA);
    }
  }
  ShowWindow(address_, fullscreen ? SW_HIDE : SW_SHOWNA);
  if (fullscreen) {
    ShowWindow(zoom_, SW_HIDE);
    ShowWindow(popups_, SW_HIDE);
  }
  UpdateIndicators();
  Layout();
  InvalidateRect(window_, nullptr, TRUE);
}

void BrowserWindow::ShowFullscreenHint(const wchar_t* text) {
  Tab* tab = ActiveTab();
  if (!tab) {
    return;
  }
  TabPanel::Content content;
  content.title = text;
  content.width = 300;
  // Over the page, inside its tab: Chromium's page would cover a panel next
  // to the tab.
  fullscreen_hint_ = std::make_unique<TabPanel>(
      tab->container(), theme_, std::move(content),
      [](const TabPanel::Answer&) {}, nullptr);
  SetTimer(window_, kFullscreenHintTimer, kFullscreenHintTime, nullptr);
}

int BrowserWindow::TabCountInProfile(const ProfileId& profile) const {
  int count = 0;
  for (const auto& [id, tab] : tabs_) {
    if (tab->profile() == profile && !tab->closing()) {
      ++count;
    }
  }
  return count;
}

void BrowserWindow::CloseTabsInProfile(const ProfileId& profile) {
  discarded_profiles_.insert(profile);  // Gone for good (005).
  std::erase_if(closed_snapshot_, [&profile](const ClosedTab& tab) {
    return tab.profile == profile;
  });
  std::vector<TabId> ids;
  for (const auto& [id, tab] : tabs_) {
    if (tab->profile() == profile) {
      ids.push_back(id);
    }
  }
  for (TabId id : ids) {
    CloseTab(id);
  }
}

void BrowserWindow::ProfilesChanged() {
  model_.SetDefaultProfile(owner_.DefaultProfile());
  ModelChanged();
}

void BrowserWindow::LayoutStrip() {
  strip_.Layout(
      StripBounds(), GetDpiForWindow(window_), model_, kind_ == Kind::kNormal,
      [this](const ProfileId& profile) { return owner_.ProfileLook(profile); });
}

void BrowserWindow::ToggleGroup(const ProfileId& profile) {
  if (strip_.IsAutoCollapsed(profile)) {
    if (std::optional<TabId> last = model_.LastActiveTabOf(profile);
        last && model_.ActivateTab(*last)) {
      ModelChanged();
      FocusActiveTab();
    }
    return;
  }
  for (const TabStripModel::Group& group : model_.groups()) {
    if (group.profile == profile &&
        model_.SetGroupCollapsed(profile, !group.collapsed)) {
      ModelChanged();
      FocusActiveTab();
      return;
    }
  }
}

void BrowserWindow::OpenTabAfter(TabId after, const std::string& url) {
  const Tab* neighbour = FindTab(after);
  const ProfileId profile =
      neighbour ? neighbour->profile() : owner_.DefaultProfile();
  const TabId id = NextTabId();
  auto tab = std::make_unique<Tab>(*this, id, window_, profile);
  tab->SetBounds(ContentBounds());
  Tab* created = tab.get();
  tabs_[id] = std::move(tab);
  model_.InsertAfter(id, after, /*activate=*/true);
  created->CreateBrowser(url.empty() ? std::string("about:blank") : url,
                         owner_.RequestContext(profile));
  ModelChanged();
  FocusActiveTab();
}

void BrowserWindow::ShowTabMenu(TabId id, POINT screen_point) {
  Tab* tab = FindTab(id);
  if (!tab) {
    return;
  }
  using Scope = TabStripModel::CloseScope;
  const bool others = !model_.TabsToClose(id, Scope::kOthers).empty();
  const bool left = !model_.TabsToClose(id, Scope::kLeft).empty();
  const bool right = !model_.TabsToClose(id, Scope::kRight).empty();
  HMENU menu = CreatePopupMenu();
  auto add = [menu](UINT command, const wchar_t* title, bool enabled) {
    AppendMenuW(menu, MF_STRING | (enabled ? 0 : MF_GRAYED), command, title);
  };
  // Chrome's tab menu.
  add(kNewTabToTheRightCommand, L"New tab to the right", true);
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  add(kReloadTabCommand, L"Reload", true);
  add(kDuplicateTabCommand, L"Duplicate", !tab->internal());
  add(kPinTabCommand, model_.IsPinned(id) ? L"Unpin" : L"Pin", true);
  add(kMuteTabCommand, tab->muted() ? L"Unmute site" : L"Mute site", true);
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  add(kCloseTabCommand, L"Close\tCtrl+W", true);
  add(kCloseOtherTabsCommand, L"Close other tabs", others);
  add(kCloseTabsLeftCommand, L"Close tabs to the left", left);
  add(kCloseTabsRightCommand, L"Close tabs to the right", right);
  const UINT command = static_cast<UINT>(TrackPopupMenuEx(
      menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
      screen_point.x, screen_point.y, window_, nullptr));
  DestroyMenu(menu);
  // The tab may have closed while the menu was open.
  tab = FindTab(id);
  if (!tab || !command) {
    return;
  }
  auto close_all = [this](const std::vector<TabId>& ids) {
    for (TabId close : ids) {
      CloseTab(close);
    }
  };
  switch (command) {
    case kNewTabToTheRightCommand:
      OpenTabAfter(id, std::string());
      break;
    case kReloadTabCommand:
      tab->Reload();
      break;
    case kDuplicateTabCommand:
      OpenTabAfter(id, tab->url());
      break;
    case kPinTabCommand:
      model_.SetPinned(id, !model_.IsPinned(id));
      ModelChanged();
      break;
    case kMuteTabCommand:
      tab->SetMuted(!tab->muted());
      break;
    case kCloseTabCommand:
      CloseTab(id);
      break;
    case kCloseOtherTabsCommand:
      close_all(model_.TabsToClose(id, Scope::kOthers));
      break;
    case kCloseTabsLeftCommand:
      close_all(model_.TabsToClose(id, Scope::kLeft));
      break;
    case kCloseTabsRightCommand:
      close_all(model_.TabsToClose(id, Scope::kRight));
      break;
    default:
      break;
  }
}

void BrowserWindow::ShowBlockedPopups() {
  Tab* tab = ActiveTab();
  if (!tab || tab->blocked_popups().empty()) {
    return;
  }
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Pop-ups blocked on this page:");
  const std::vector<std::string> urls = tab->blocked_popups();
  for (size_t i = 0; i < urls.size(); ++i) {
    std::wstring title = Utf8ToWide(urls[i]);
    if (title.size() > 70) {
      title = title.substr(0, 67) + L"\u2026";
    }
    AppendMenuW(menu, MF_STRING,
                kFirstBlockedPopupCommand + static_cast<UINT>(i),
                title.c_str());
  }
  const CefRefPtr<CefBrowser> browser = tab->browser();
  const std::string origin = browser ? GetSiteInfo(browser).origin : "";
  if (!origin.empty()) {
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    const std::wstring allow =
        L"Always allow pop-ups and redirects from " + Utf8ToWide(origin);
    AppendMenuW(menu, MF_STRING, kAllowPopupsCommand, allow.c_str());
  }
  RECT button = {};
  GetWindowRect(popups_, &button);
  const UINT command = static_cast<UINT>(TrackPopupMenuEx(
      menu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
      button.right, button.bottom, window_, nullptr));
  DestroyMenu(menu);
  // The tab may have gone while the menu was open.
  tab = ActiveTab();
  if (!tab || !command) {
    return;
  }
  if (command == kAllowPopupsCommand) {
    if (CefRefPtr<CefBrowser> current = tab->browser()) {
      SetSitePermission(current, origin, PermissionKind::kPopups,
                        PermissionSetting::kAllow);
    }
    tab->ClearBlockedPopups();
  } else if (command >= kFirstBlockedPopupCommand &&
             command - kFirstBlockedPopupCommand < urls.size()) {
    Tab* popup = InsertTabFromOpener(tab, true);
    popup->CreateBrowser(urls[command - kFirstBlockedPopupCommand],
                         owner_.RequestContext(popup->profile()));
    FocusActiveTab();
  }
}

void BrowserWindow::DrawIndicator(const DRAWITEMSTRUCT& item) {
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  SetDCBrushColor(item.hDC, theme_.field);
  FillRect(item.hDC, &item.rcItem,
           static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  HGDIOBJ pen = SelectObject(item.hDC, GetStockObject(DC_PEN));
  HGDIOBJ brush = SelectObject(item.hDC, GetStockObject(DC_BRUSH));
  SetDCPenColor(item.hDC, theme_.separator);
  SetDCBrushColor(item.hDC, pressed ? theme_.hover : theme_.field);
  const int radius = item.rcItem.bottom - item.rcItem.top;
  RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right,
            item.rcItem.bottom, radius, radius);
  SelectObject(item.hDC, brush);
  SelectObject(item.hDC, pen);
  const std::wstring text = WindowText(item.hwndItem);
  // The pop-up indicator is an icon, the zoom indicator text.
  HGDIOBJ previous = SelectObject(
      item.hDC, item.hwndItem == popups_ ? icon_font_ : text_font_);
  SetBkMode(item.hDC, TRANSPARENT);
  // The pop-up icon in orange, as on macOS.
  SetTextColor(item.hDC,
               item.hwndItem == popups_ ? RGB(0xE8, 0x6E, 0x00) : theme_.text);
  RECT area = item.rcItem;
  DrawTextW(item.hDC, text.c_str(), -1, &area,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(item.hDC, previous);
}

void BrowserWindow::UpdateIndicators() {
  Tab* tab = ActiveTab();
  const double level = tab ? tab->ZoomLevel() : 0.0;
  const bool shown = !fullscreen_ && IsZoomIndicatorShown(level);
  if (shown) {
    const int percent = ZoomPercentFromLevel(level);
    const std::wstring text = std::to_wstring(percent) + L" %";
    if (WindowText(zoom_) != text) {
      SetWindowTextW(zoom_, text.c_str());
      const std::wstring name =
          L"Zoom " + std::to_wstring(percent) + L" percent, reset";
      SetAccessibleName(zoom_, name.c_str());
      InvalidateRect(zoom_, nullptr, FALSE);
    }
  }
  const bool popups_shown =
      !fullscreen_ && tab && !tab->blocked_popups().empty();
  if (shown != HasVisibleStyle(zoom_) ||
      popups_shown != HasVisibleStyle(popups_)) {
    ShowWindow(zoom_, shown ? SW_SHOWNA : SW_HIDE);
    ShowWindow(popups_, popups_shown ? SW_SHOWNA : SW_HIDE);
    Layout();
  }
}

void BrowserWindow::ShowAppMenu() {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kNewTabCommand, L"New tab\tCtrl+T");
  AppendMenuW(menu, MF_STRING, kNewWindowCommand, L"New window\tCtrl+N");
  // Profiles (005): a new tab in one, or the profile manager.
  HMENU profiles = CreatePopupMenu();
  const std::vector<ProfileInfo> profile_list = owner_.ProfileList();
  const ProfileId default_profile = owner_.DefaultProfile();
  for (size_t i = 0; i < profile_list.size(); ++i) {
    std::wstring title = L"New tab in " + Utf8ToWide(profile_list[i].name);
    if (profile_list[i].id == default_profile) {
      title += L" (default)";
    }
    AppendMenuW(profiles, MF_STRING,
                kFirstProfileCommand + static_cast<UINT>(i), title.c_str());
  }
  AppendMenuW(profiles, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(profiles, MF_STRING, kManageProfilesCommand,
              L"Manage profiles\u2026");
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(profiles),
              L"Profiles");
  // History (013), as Chrome's: the history page, clearing it, recently
  // closed tabs and windows (newest first) and recently visited pages of
  // the active tab's profile.
  HMENU closed = CreatePopupMenu();
  AppendMenuW(closed, MF_STRING, kHistoryCommand, L"History\tCtrl+H");
  AppendMenuW(closed, MF_STRING, kClearBrowsingDataCommand,
              L"Clear browsing data\u2026\tCtrl+Shift+Del");
  AppendMenuW(closed, MF_SEPARATOR, 0, nullptr);
  std::vector<std::int64_t> closed_ids;
  const std::deque<ClosedEntry>& entries = owner_.RecentlyClosed();
  AppendMenuW(closed, MF_STRING | (entries.empty() ? MF_GRAYED : 0),
              kReopenClosedTabCommand, L"Reopen closed tab\tCtrl+Shift+T");
  for (const ClosedEntry& entry : entries) {
    const ClosedTab& first = entry.tabs.front();
    const std::wstring title =
        entry.is_window
            ? L"Window (" + std::to_wstring(entry.tabs.size()) +
                  (entry.tabs.size() == 1 ? L" tab)" : L" tabs)")
            : MenuText(first.title.empty() ? first.url : first.title);
    AppendMenuW(closed, MF_STRING,
                kFirstClosedCommand + static_cast<UINT>(closed_ids.size()),
                title.c_str());
    closed_ids.push_back(entry.id);
  }
  AppendMenuW(closed, MF_SEPARATOR, 0, nullptr);
  const Tab* active_tab = ActiveTab();
  const ProfileId recent_profile =
      active_tab ? active_tab->profile() : owner_.DefaultProfile();
  std::vector<std::string> recent_urls;
  if (const std::vector<HistoryMatch>* recent =
          owner_.RecentPages(recent_profile)) {
    AppendMenuW(
        closed, MF_STRING | MF_GRAYED, 0,
        recent->empty() ? L"No recently visited pages" : L"Recently visited");
    for (const HistoryMatch& page : *recent) {
      AppendMenuW(
          closed, MF_STRING,
          kFirstRecentPageCommand + static_cast<UINT>(recent_urls.size()),
          MenuText(page.title.empty() ? page.url : page.title).c_str());
      recent_urls.push_back(page.url);
    }
  } else {
    // Read from the history in the background; shown next time.
    AppendMenuW(closed, MF_STRING | MF_GRAYED, 0, L"Loading\u2026");
  }
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(closed), L"History");
  AppendMenuW(menu, MF_STRING, kDownloadsCommand, L"Downloads\tCtrl+J");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kZoomInCommand, L"Zoom in\tCtrl++");
  AppendMenuW(menu, MF_STRING, kZoomOutCommand, L"Zoom out\tCtrl+-");
  AppendMenuW(menu, MF_STRING, kZoomResetCommand, L"Actual size\tCtrl+0");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kPrintCommand, L"Print\u2026\tCtrl+P");
  AppendMenuW(menu, MF_STRING, kFindCommand, L"Find\u2026\tCtrl+F");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kSettingsCommand, L"Settings");
  AppendMenuW(menu, MF_STRING, kHelpCommand, L"Brouk Help\tF1");
  AppendMenuW(menu, MF_STRING, kLicencesCommand, L"Open-source licences");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit");
  RECT button = {};
  GetWindowRect(menu_, &button);
  const UINT command = static_cast<UINT>(TrackPopupMenuEx(
      menu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
      button.right, button.bottom, window_, nullptr));
  DestroyMenu(menu);  // With its submenus.
  if (command >= kFirstClosedCommand &&
      command - kFirstClosedCommand < closed_ids.size()) {
    owner_.ReopenClosed(closed_ids[command - kFirstClosedCommand]);
    return;
  }
  if (command >= kFirstRecentPageCommand &&
      command - kFirstRecentPageCommand < recent_urls.size()) {
    OpenTab(recent_urls[command - kFirstRecentPageCommand], true,
            recent_profile);
    return;
  }
  if (command >= kFirstProfileCommand &&
      command - kFirstProfileCommand < profile_list.size()) {
    OpenTab(std::string(), true,
            profile_list[command - kFirstProfileCommand].id);
    return;
  }
  switch (command) {
    case kManageProfilesCommand:
      owner_.OpenProfileManager();
      break;
    case kNewTabCommand:
      OpenTab(std::string(), true);
      break;
    case kNewWindowCommand:
      owner_.OpenWindow(std::string(), ProfileId());
      break;
    case kHelpCommand:
      owner_.OpenHelp(std::string());
      break;
    case kSettingsCommand:
      owner_.OpenSettings(std::string());
      break;
    case kZoomInCommand:
    case kZoomOutCommand:
    case kZoomResetCommand:
      if (Tab* tab = ActiveTab()) {
        tab->Zoom(command == kZoomInCommand    ? CEF_ZOOM_COMMAND_IN
                  : command == kZoomOutCommand ? CEF_ZOOM_COMMAND_OUT
                                               : CEF_ZOOM_COMMAND_RESET);
      }
      break;
    case kPrintCommand:
      if (Tab* tab = ActiveTab()) {
        tab->Print();
      }
      break;
    case kFindCommand:
      if (Tab* tab = ActiveTab()) {
        tab->ShowFindBar();
      }
      break;
    case kLicencesCommand:
      OpenInternalPage("brouk://credits/");
      break;
    case kExitCommand:
      owner_.CloseAllWindows();
      break;
    case kReopenClosedTabCommand:
      owner_.ReopenClosed(std::nullopt);
      break;
    case kHistoryCommand:
      OpenInternalPage("brouk://history/");
      break;
    case kClearBrowsingDataCommand:
      owner_.OpenClearBrowsingData();
      break;
    case kDownloadsCommand:
      ShowDownloads();
      break;
    default:
      break;
  }
}

void BrowserWindow::RefreshTheme() {
  for (const auto& [id, tab] : tabs_) {
    tab->ApplyColorScheme();
  }
  theme_ = CurrentTheme();
  ApplyThemeToFrame(window_, theme_);
  UpdateFonts();
  RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void BrowserWindow::SendRendererConfig(bool json_viewer) {
  for (const auto& [id, tab] : tabs_) {
    tab->SendRendererConfig(json_viewer);
  }
}

void BrowserWindow::OpenInternalPage(const std::string& url) {
  const std::optional<std::string> page = InternalPageOf(url);
  if (!page) {
    return;
  }
  if (*page == "help") {
    owner_.OpenHelp(std::string());
    return;
  }
  if (*page == "profiles") {
    owner_.OpenProfileManager();  // Its own window, as on macOS.
    return;
  }
  if (*page == "settings") {
    owner_.OpenSettings(url);  // Its own window, as on macOS.
    return;
  }
  Tab* active = ActiveTab();
  if (active && active->internal() && !active->closing() &&
      InternalPageOf(active->url()) == page) {
    if (url != active->url()) {
      active->Navigate(url);  // Same page: only the fragment changes.
    }
    FocusActiveTab();
    return;
  }
  // In place of an empty tab, as Chrome loads chrome:// pages there.
  const bool replace = active && IsBlank(active->url()) && !active->internal();
  Tab* tab = InsertTabFromOpener(active, true);
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler =
      owner_.InternalPageHandler(*page, *this, tab->id());
  if (!handler) {
    RemoveUncreatedTab(tab->id());  // Not on Windows yet.
    return;
  }
  tab->CreateInternalPage(url, std::move(handler), owner_.InternalContext());
  if (replace) {
    replaced_tabs_.insert(active->id());
    CloseTab(active->id());
  }
  FocusActiveTab();
}

void BrowserWindow::OpenPageFromTab(TabId from,
                                    const ProfileId& requested,
                                    const std::string& url,
                                    bool new_tab) {
  Tab* source = FindTab(from);
  const bool exists = std::ranges::any_of(
      owner_.ProfileList(),
      [&requested](const ProfileInfo& p) { return p.id == requested; });
  const ProfileId profile = exists ? requested : ProfileOfTab(from);
  if (!source || source->closing() || source->profile() != profile) {
    OpenTab(url, !new_tab, profile);
    if (!new_tab) {
      Activate();
    }
    return;
  }
  // The history tab cannot load web pages (internal context): a web tab
  // takes its place, or opens next to it.
  Tab* web = InsertTabFromOpener(source, !new_tab);
  web->CreateBrowser(url, owner_.RequestContext(profile));
  if (!new_tab) {
    replaced_tabs_.insert(from);
    CloseTab(from);
    FocusActiveTab();
  }
}

ProfileId BrowserWindow::ProfileOfTab(TabId id) {
  const Tab* tab = FindTab(id);
  return tab ? tab->profile() : owner_.DefaultProfile();
}

CefRefPtr<CefBrowser> BrowserWindow::AnyBrowserInProfile(
    const ProfileId& profile) const {
  for (const auto& [id, tab] : tabs_) {
    if (tab->profile() == profile && !tab->internal() && !tab->closing() &&
        tab->browser()) {
      return tab->browser();
    }
  }
  return nullptr;
}

HistoryService* BrowserWindow::TabHistory() {
  return owner_.History();
}

void BrowserWindow::OnTabHistoryChanged(Tab& tab) {
  owner_.OnHistoryChanged(tab.profile());
}

std::optional<SessionWindow> BrowserWindow::SessionState() const {
  if (kind_ != Kind::kNormal || closing_ || !window_ || model_.empty()) {
    return std::nullopt;
  }
  SessionWindow saved;
  for (std::size_t i = 0; i < model_.count(); ++i) {
    const TabId id = model_.id_at(i);
    auto it = tabs_.find(id);
    if (it == tabs_.end() || it->second->closing()) {
      continue;
    }
    const Tab& tab = *it->second;
    if (model_.active_id() == id) {
      saved.active = saved.tabs.size();
    }
    saved.tabs.push_back({.url = tab.url(),
                          .title = tab.title(),
                          .profile = tab.profile(),
                          .pinned = model_.IsPinned(id)});
  }
  if (saved.tabs.empty()) {
    return std::nullopt;
  }
  for (const TabStripModel::Group& group : model_.groups()) {
    if (group.collapsed) {
      saved.collapsed.push_back(group.profile);
    }
  }
  // The frame to go back to from maximised (and from full screen).
  WINDOWPLACEMENT placement = {};
  placement.length = sizeof(WINDOWPLACEMENT);
  if (fullscreen_) {
    placement = saved_placement_;
  } else if (!GetWindowPlacement(window_, &placement)) {
    return saved;
  }
  const RECT& normal = placement.rcNormalPosition;
  saved.x = normal.left;
  saved.y = normal.top;
  saved.width = normal.right - normal.left;
  saved.height = normal.bottom - normal.top;
  saved.maximized = placement.showCmd == SW_SHOWMAXIMIZED ||
                    (placement.showCmd == SW_SHOWMINIMIZED &&
                     (placement.flags & WPF_RESTORETOMAXIMIZED) != 0);
  return saved;
}

void BrowserWindow::Restore(const SessionWindow& saved) {
  if (saved.width > 0) {
    // Workspace coordinates (as GetWindowPlacement gives them), only if
    // still on a monitor: displays may have changed.
    const RECT frame = {saved.x, saved.y, saved.x + saved.width,
                        saved.y + saved.height};
    if (MonitorFromRect(&frame, MONITOR_DEFAULTTONULL)) {
      WINDOWPLACEMENT placement = {};
      placement.length = sizeof(WINDOWPLACEMENT);
      GetWindowPlacement(window_, &placement);
      placement.flags = 0;
      placement.showCmd = SW_HIDE;  // Shown by Show().
      placement.rcNormalPosition = frame;
      SetWindowPlacement(window_, &placement);
    }
  }
  if (saved.maximized) {
    show_command_ = SW_SHOWMAXIMIZED;
  }
  const std::vector<ProfileInfo>& profiles = owner_.ProfileList();
  std::optional<TabId> active;
  for (std::size_t i = 0; i < saved.tabs.size(); ++i) {
    const SessionTab& saved_tab = saved.tabs[i];
    // Tabs of a deleted profile open in the default profile.
    const bool exists = std::ranges::any_of(
        profiles,
        [&](const ProfileInfo& p) { return p.id == saved_tab.profile; });
    const ProfileId profile =
        exists ? saved_tab.profile : owner_.DefaultProfile();
    const TabId id = NextTabId();
    std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler;
    if (IsInternalPageUrl(saved_tab.url)) {
      // Pages with their own window (help, settings) do not come back.
      const std::optional<std::string> page = InternalPageOf(saved_tab.url);
      handler = page ? owner_.InternalPageHandler(*page, *this, id) : nullptr;
      if (!handler) {
        continue;
      }
    }
    auto tab = std::make_unique<Tab>(*this, id, window_, profile);
    tab->SetBounds(ContentBounds());
    Tab* created = tab.get();
    tabs_[id] = std::move(tab);
    model_.AppendTab(id, profile, false);
    if (saved_tab.pinned) {
      model_.SetPinned(id, true);
    }
    if (handler) {
      created->CreateInternalPage(saved_tab.url, std::move(handler),
                                  owner_.InternalContext());
    } else if (i == saved.active) {
      created->CreateBrowser(saved_tab.url, owner_.RequestContext(profile));
    } else {
      created->DeferBrowser(saved_tab.url, saved_tab.title,
                            owner_.RequestContext(profile));
    }
    if (i == saved.active || !active) {
      active = id;
    }
  }
  if (active) {
    model_.ActivateTab(*active);
  }
  for (const ProfileId& profile : saved.collapsed) {
    model_.SetGroupCollapsed(profile, true);
  }
  ModelChanged();
}

bool BrowserWindow::HasOnlyNewTab() const {
  if (tabs_.size() != 1) {
    return false;
  }
  const Tab& tab = *tabs_.begin()->second;
  return !tab.internal() && !tab.closing() && IsBlank(tab.url());
}

void BrowserWindow::ShowRestorePagesBar(std::function<void()> restore) {
  if (Tab* tab = ActiveTab()) {
    tab->ShowRestorePagesPanel(std::move(restore));
  }
}

Tab* BrowserWindow::FindTab(TabId id) {
  auto it = tabs_.find(id);
  return it == tabs_.end() ? nullptr : it->second.get();
}

Tab* BrowserWindow::ActiveTab() {
  const std::optional<TabId> active = model_.active_id();
  return active ? FindTab(*active) : nullptr;
}

void BrowserWindow::OpenTab(const std::string& url,
                            bool foreground,
                            const ProfileId& requested) {
  const ProfileId profile =
      requested.empty() ? owner_.DefaultProfile() : requested;
  if (IsInternalPageUrl(url)) {
    // From the command line or a link: brouk:// pages load only in their
    // own request context.
    if (std::optional<std::string> page = InternalPageOf(url)) {
      if (*page == "help") {
        owner_.OpenHelp(std::string());
        return;
      }
      if (*page == "profiles") {
        owner_.OpenProfileManager();
        return;
      }
      if (*page == "settings") {
        owner_.OpenSettings(url);
        return;
      }
      const TabId id = NextTabId();
      if (auto handler = owner_.InternalPageHandler(*page, *this, id)) {
        auto tab = std::make_unique<Tab>(*this, id, window_, profile);
        tab->SetBounds(ContentBounds());
        Tab* created = tab.get();
        tabs_[id] = std::move(tab);
        model_.AppendTab(id, profile, foreground);
        created->CreateInternalPage(url, std::move(handler),
                                    owner_.InternalContext());
        ModelChanged();
        return;
      }
    }
  }
  const TabId id = NextTabId();
  auto tab = std::make_unique<Tab>(*this, id, window_, profile);
  tab->SetBounds(ContentBounds());
  Tab* created = tab.get();
  tabs_[id] = std::move(tab);
  model_.AppendTab(id, profile, foreground);
  created->CreateBrowser(url.empty() ? std::string("about:blank") : url,
                         owner_.RequestContext(profile));
  ModelChanged();
  if (foreground) {
    FocusActiveTab();
  }
}

Tab* BrowserWindow::InsertTabFromOpener(const Tab* opener, bool foreground) {
  const ProfileId profile =
      opener ? opener->profile() : owner_.DefaultProfile();
  const TabId id = NextTabId();
  auto tab = std::make_unique<Tab>(*this, id, window_, profile);
  tab->SetBounds(ContentBounds());
  Tab* created = tab.get();
  tabs_[id] = std::move(tab);
  if (opener && model_.IndexOf(opener->id())) {
    model_.InsertFromOpener(id, opener->id(), foreground);
  } else {
    model_.AppendTab(id, profile, foreground);
  }
  ModelChanged();
  return created;
}

void BrowserWindow::RemoveUncreatedTab(TabId id) {
  model_.RemoveTab(id);
  tabs_.erase(id);
  if (tabs_.empty()) {
    PostMessageW(window_, WM_CLOSE, 0, 0);
    return;
  }
  ModelChanged();
}

void BrowserWindow::CloseTab(TabId id) {
  if (Tab* tab = FindTab(id); tab && !tab->closing()) {
    tab->Close();
  }
}

void BrowserWindow::ModelChanged() {
  CloseSuggestions();  // Another tab, or the list's tab changed.
  const std::optional<TabId> active = model_.active_id();
  // Like Chrome, switching tabs ends a page's full screen.
  if (fullscreen_tab_ && fullscreen_tab_ != active) {
    if (Tab* tab = FindTab(*fullscreen_tab_)) {
      tab->ExitFullscreen();
    }
  }
  for (const auto& [id, tab] : tabs_) {
    tab->SetVisible(active == id);
  }
  // A restored tab gets its browser when first shown (015).
  if (Tab* tab = ActiveTab(); tab && tab->has_deferred_browser()) {
    tab->CreateDeferredBrowser();
  }
  owner_.OnSessionChanged();
  LayoutStrip();
  UpdateToolbar();
  UpdateSpinner();
  InvalidateStrip();
}

ClosedTab BrowserWindow::ClosedTabFor(const Tab& tab, std::size_t index) const {
  std::size_t start = index;
  for (const TabStripModel::Group& group : model_.groups()) {
    if (group.profile == tab.profile()) {
      start = group.start;
    }
  }
  return {.url = tab.url(),
          .title = tab.title(),
          .profile = tab.profile(),
          .pinned = model_.IsPinned(tab.id()),
          .window_id = window_id_,
          .position = index - start};
}

void BrowserWindow::RestoreClosedTab(const ClosedTab& closed, bool activate) {
  const TabId id = NextTabId();
  auto tab = std::make_unique<Tab>(*this, id, window_, closed.profile);
  tab->SetBounds(ContentBounds());
  Tab* created = tab.get();
  tabs_[id] = std::move(tab);
  model_.InsertInGroup(id, closed.profile, closed.position, activate);
  if (closed.pinned) {
    model_.SetPinned(id, true);
  }
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler;
  if (const std::optional<std::string> page = InternalPageOf(closed.url)) {
    handler = owner_.InternalPageHandler(*page, *this, id);
  }
  if (handler) {
    created->CreateInternalPage(closed.url, std::move(handler),
                                owner_.InternalContext());
  } else {
    created->CreateBrowser(
        IsInternalPageUrl(closed.url) ? std::string("about:blank") : closed.url,
        owner_.RequestContext(closed.profile));
  }
  ModelChanged();
  if (activate) {
    FocusActiveTab();
  }
}

void BrowserWindow::FocusActiveTab() {
  Tab* tab = ActiveTab();
  if (!tab) {
    return;
  }
  // New and blank tabs focus the address bar, others the page (Chrome).
  if (IsBlank(tab->url()) && kind_ == Kind::kNormal) {
    FocusAddressBar();
  } else {
    tab->Focus();
  }
}

void BrowserWindow::UpdateToolbar() {
  Tab* tab = ActiveTab();
  EnableWindow(back_, tab && tab->can_go_back());
  EnableWindow(forward_, tab && tab->can_go_forward());
  if (GetFocus() != address_) {
    ShowAddress();
  }
  UpdateIndicators();
  // "Page - Brouk", as Chrome's "Page - Google Chrome".
  std::wstring title = Utf8ToWide(kAppName);
  if (tab) {
    title = Utf8ToWide(tab->DisplayTitle()) + L" - " + title;
  }
  SetWindowTextW(window_, title.c_str());
}

void BrowserWindow::UpdateSpinner() {
  const bool loading = std::ranges::any_of(
      tabs_, [](const auto& entry) { return entry.second->loading(); });
  if (loading && !spinner_running_) {
    SetTimer(window_, kSpinnerTimer, kSpinnerInterval, nullptr);
    spinner_running_ = true;
  } else if (!loading && spinner_running_) {
    KillTimer(window_, kSpinnerTimer);
    spinner_running_ = false;
  }
}

void BrowserWindow::FocusAddressBar() {
  SetFocus(address_);
  SendMessageW(address_, EM_SETSEL, 0, -1);
}

void BrowserWindow::ShowAddress() {
  Tab* tab = ActiveTab();
  // A new, empty tab shows an empty address bar (Chrome).
  const std::string shown =
      tab && !IsBlank(tab->url()) ? tab->url() : std::string();
  setting_address_ = true;
  SetWindowTextW(address_, Utf8ToWide(shown).c_str());
  setting_address_ = false;
}

void BrowserWindow::SetAddressText(const std::wstring& text, int select_from) {
  setting_address_ = true;
  SetWindowTextW(address_, text.c_str());
  setting_address_ = false;
  const int end = static_cast<int>(text.size());
  SendMessageW(address_, EM_SETSEL, select_from < 0 ? end : select_from, end);
}

void BrowserWindow::OnAddressEdited() {
  if (setting_address_ || GetFocus() != address_ || kind_ == Kind::kPopup) {
    return;
  }
  const std::wstring text = WindowText(address_);
  DWORD start = 0;
  DWORD end = 0;
  SendMessageW(address_, EM_GETSEL, reinterpret_cast<WPARAM>(&start),
               reinterpret_cast<LPARAM>(&end));
  allow_inline_ = start == end && end == text.size() &&
                  text.size() > typed_text_.size() &&
                  text.starts_with(typed_text_);
  typed_text_ = text;
  UpdateSuggestions();
}

void BrowserWindow::CloseSuggestions() {
  ++suggest_generation_;
  if (suggest_request_) {
    suggest_request_->Cancel();
    suggest_request_ = nullptr;
  }
  history_matches_.clear();
  search_suggestions_.clear();
  suggestion_rows_.clear();
  suggesting_ = false;
  if (suggestions_) {
    suggestions_->Hide();
  }
}

void BrowserWindow::UpdateSuggestions() {
  Tab* tab = ActiveTab();
  const std::string typed = WideToUtf8(typed_text_);
  if (!tab || typed.find_first_not_of(" \t") == std::string::npos) {
    CloseSuggestions();
    return;
  }
  const std::uint64_t generation = ++suggest_generation_;
  suggesting_ = true;
  history_matches_.clear();
  ShowSuggestions();
  std::weak_ptr<int> alive = alive_;
  HistoryService* history = owner_.History();
  if (history && owner_.HistorySuggestionsEnabled()) {
    history->Suggest(
        tab->profile(), typed, kMaxHistorySuggestions,
        [this, alive, generation](std::vector<HistoryMatch> m) {
          if (alive.expired() || generation != suggest_generation_) {
            return;
          }
          history_matches_ = std::move(m);
          ShowSuggestions();
        });
  }
  if (suggest_request_) {
    suggest_request_->Cancel();
    suggest_request_ = nullptr;
  }
  const std::string suggest_template = owner_.SearchSuggestTemplate();
  if (suggest_template.empty() || !ShouldFetchSearchSuggestions(typed)) {
    search_suggestions_.clear();
    return;
  }
  suggest_request_ = SearchSuggestRequest::Start(
      owner_.RequestContext(tab->profile()),
      BuildSearchUrl(typed, suggest_template),
      [this, alive, generation](std::vector<std::string> suggestions) {
        if (alive.expired() || generation != suggest_generation_) {
          return;
        }
        suggest_request_ = nullptr;
        search_suggestions_ = std::move(suggestions);
        ShowSuggestions();
      });
}

void BrowserWindow::ShowSuggestions() {
  if (!suggesting_ || GetFocus() != address_) {
    return;
  }
  const TabClientDelegate::SearchEngine engine = owner_.SearchEngine();
  const std::string typed = WideToUtf8(typed_text_);
  suggestion_rows_ =
      BuildAddressSuggestions({.text = typed,
                               .search_template = engine.url_template,
                               .search_engine_name = engine.name,
                               .history = history_matches_,
                               .search_suggestions = search_suggestions_,
                               .bookmarks = {}});
  if (!suggestions_) {
    suggestions_ = std::make_unique<SuggestionsPopup>(
        window_, [this](int row) { OpenSuggestionRow(row); });
  }
  RECT anchor = address_field_;
  MapWindowPoints(window_, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
  suggestions_->Show(suggestion_rows_, anchor, theme_, text_font_, icon_font_);
  // The first row's inline completion: the rest of an address typed
  // before, selected so that typing on replaces it.
  if (allow_inline_ && !suggestion_rows_.empty() &&
      !suggestion_rows_.front().inline_completion.empty() &&
      WindowText(address_) == typed_text_) {
    SetAddressText(
        typed_text_ + Utf8ToWide(suggestion_rows_.front().inline_completion),
        static_cast<int>(typed_text_.size()));
  }
}

bool BrowserWindow::HandleSuggestionKey(int key, bool shift) {
  if (!suggestions_ || !suggestions_->visible() || suggestion_rows_.empty()) {
    return false;
  }
  if (key == VK_DOWN || key == VK_UP) {
    const int row =
        std::clamp(suggestions_->selected() + (key == VK_DOWN ? 1 : -1), 0,
                   suggestions_->count() - 1);
    suggestions_->Select(row);
    // The field shows the row's address or query; typed rows the typed
    // text.
    const AddressSuggestion& suggestion =
        suggestion_rows_[static_cast<std::size_t>(row)];
    switch (suggestion.kind) {
      case AddressSuggestion::Kind::kHistory:
      case AddressSuggestion::Kind::kBookmark:
        SetAddressText(Utf8ToWide(suggestion.url));
        break;
      case AddressSuggestion::Kind::kSearch:
        SetAddressText(Utf8ToWide(suggestion.text));
        break;
      case AddressSuggestion::Kind::kTypedUrl:
      case AddressSuggestion::Kind::kTypedSearch:
        SetAddressText(typed_text_);
        break;
    }
    return true;
  }
  if (key == VK_ESCAPE) {
    // The first Escape closes the list and drops the inline completion;
    // the next one restores the page's address.
    SetAddressText(typed_text_);
    CloseSuggestions();
    return true;
  }
  // Shift+Delete removes a history suggestion from the profile's history
  // (013), as in Chrome.
  const int row = suggestions_->selected();
  if (key == VK_DELETE && shift && row >= 0 && row < suggestions_->count() &&
      suggestion_rows_[static_cast<std::size_t>(row)].kind ==
          AddressSuggestion::Kind::kHistory) {
    const std::string url = suggestion_rows_[static_cast<std::size_t>(row)].url;
    Tab* tab = ActiveTab();
    if (HistoryService* history = owner_.History(); history && tab) {
      history->DeletePage(tab->profile(), url);
      owner_.OnHistoryChanged(tab->profile());
    }
    std::erase_if(history_matches_, [&url](const HistoryMatch& match) {
      return match.url == url;
    });
    SetAddressText(typed_text_);
    ShowSuggestions();
    return true;
  }
  return false;
}

void BrowserWindow::OpenSuggestionRow(int row) {
  if (row < 0 || row >= static_cast<int>(suggestion_rows_.size())) {
    return;
  }
  const AddressSuggestion suggestion =
      suggestion_rows_[static_cast<std::size_t>(row)];
  CloseSuggestions();
  // Addresses and history pages count as typed (Chrome); searches do not.
  NavigateActiveTab(suggestion.url,
                    suggestion.kind == AddressSuggestion::Kind::kTypedUrl ||
                        suggestion.kind == AddressSuggestion::Kind::kHistory);
}

void BrowserWindow::NavigateToAddressBarText() {
  if (!ActiveTab() || kind_ == Kind::kPopup) {
    return;
  }
  if (suggestions_ && suggestions_->visible() &&
      suggestions_->selected() >= 0) {
    OpenSuggestionRow(suggestions_->selected());
    return;
  }
  CloseSuggestions();
  const TabClientDelegate::SearchEngine engine = owner_.SearchEngine();
  const AddressInputResult input = ClassifyAddressInput(
      WideToUtf8(WindowText(address_)), engine.url_template);
  if (input.kind == AddressInputResult::Kind::kEmpty) {
    return;
  }
  // Typed addresses count for history ranking; searches do not (Chrome).
  NavigateActiveTab(input.url, input.kind == AddressInputResult::Kind::kUrl);
}

void BrowserWindow::NavigateActiveTab(const std::string& url, bool typed) {
  Tab* tab = ActiveTab();
  if (!tab) {
    return;
  }
  if (IsInternalPageUrl(url)) {
    OpenInternalPage(url);
    return;
  }
  if (tab->internal()) {
    // An internal page's tab cannot load web pages: a web tab takes its
    // place.
    const TabId internal = tab->id();
    Tab* web = InsertTabFromOpener(tab, true);
    if (typed) {
      web->MarkNextVisitTyped();
    }
    web->CreateBrowser(url, owner_.RequestContext(web->profile()));
    replaced_tabs_.insert(internal);
    CloseTab(internal);
    FocusActiveTab();
    return;
  }
  if (typed) {
    tab->MarkNextVisitTyped();
  }
  tab->Navigate(url);
  tab->Focus();
}

bool BrowserWindow::HandleShortcut(int key,
                                   bool control,
                                   bool alt,
                                   bool shift) {
  // Chrome's Windows shortcuts for what the Windows app has so far.
  Tab* tab = ActiveTab();
  if (!control && !alt && !shift && key == VK_F1) {
    owner_.OpenHelp(std::string());
    return true;
  }
  if ((control && !alt && key == 'L') || (alt && !control && key == 'D') ||
      (!control && !alt && key == VK_F6)) {
    FocusAddressBar();
    return true;
  }
  if ((!control && !alt && key == VK_F5) ||
      (control && !alt && !shift && key == 'R')) {
    if (tab) {
      tab->Reload();
    }
    return true;
  }
  if (alt && !control && (key == VK_LEFT || key == VK_RIGHT)) {
    if (tab && key == VK_LEFT) {
      tab->GoBack();
    } else if (tab) {
      tab->GoForward();
    }
    return true;
  }
  if (!control && !alt && !shift && key == VK_F11) {
    if (fullscreen_tab_) {
      // Leaves the page's full screen, and with it the window's.
      if (Tab* fullscreen_tab = FindTab(*fullscreen_tab_)) {
        fullscreen_tab->ExitFullscreen();
      }
    } else {
      fullscreen_for_page_ = false;
      SetFullscreen(!fullscreen_);
      if (fullscreen_) {
        ShowFullscreenHint(L"Press F11 to exit full screen");
      }
    }
    return true;
  }
  if (control && !alt && !shift && key == 'P') {
    if (tab) {
      tab->Print();
    }
    return true;
  }
  if (control && !alt && !shift && key == 'F') {
    if (tab) {
      tab->ShowFindBar();
    }
    return true;
  }
  // F3 and Ctrl+G: next match; with Shift the previous one.
  if ((!control && !alt && key == VK_F3) || (control && !alt && key == 'G')) {
    if (tab) {
      tab->FindNext(!shift);
    }
    return true;
  }
  if (control && !alt) {
    std::optional<cef_zoom_command_t> zoom;
    if (key == VK_OEM_PLUS || key == VK_ADD) {
      zoom = CEF_ZOOM_COMMAND_IN;
    } else if (key == VK_OEM_MINUS || key == VK_SUBTRACT) {
      zoom = CEF_ZOOM_COMMAND_OUT;
    } else if (!shift && (key == '0' || key == VK_NUMPAD0)) {
      zoom = CEF_ZOOM_COMMAND_RESET;
    }
    if (zoom) {
      if (tab) {
        tab->Zoom(*zoom);
      }
      return true;
    }
  }
  if (kind_ == Kind::kPopup) {
    return false;
  }
  if (control && !alt && !shift && key == 'T') {
    OpenTab(std::string(), true);
    return true;
  }
  if (control && !alt && !shift && key == 'J') {
    ShowDownloads();
    return true;
  }
  if (control && !alt && !shift && key == 'H') {
    OpenInternalPage("brouk://history/");
    return true;
  }
  if (control && !alt && shift && key == VK_DELETE) {
    owner_.OpenClearBrowsingData();
    return true;
  }
  if (control && !alt && shift && key == 'T') {
    owner_.ReopenClosed(std::nullopt);
    return true;
  }
  if (control && !alt && !shift && key == 'N') {
    owner_.OpenWindow(std::string(), ProfileId());
    return true;
  }
  if (control && !alt && shift && key == 'W') {
    PostMessageW(window_, WM_CLOSE, 0, 0);
    return true;
  }
  if (control && !alt && !shift && (key == 'W' || key == VK_F4)) {
    if (tab) {
      CloseTab(tab->id());
    }
    return true;
  }
  const bool next = (key == VK_TAB && !shift) || (key == VK_NEXT && !shift);
  const bool previous = (key == VK_TAB && shift) || (key == VK_PRIOR && !shift);
  if (control && !alt && (next || previous)) {
    if (next) {
      model_.ActivateNext();
    } else {
      model_.ActivatePrevious();
    }
    ModelChanged();
    FocusActiveTab();
    return true;
  }
  if (control && !alt && !shift && key >= '1' && key <= '9') {
    model_.ActivateByShortcutNumber(key - '0');
    ModelChanged();
    FocusActiveTab();
    return true;
  }
  return false;
}

void BrowserWindow::OnTabChanged(Tab& tab) {
  owner_.OnSessionChanged();
  if (model_.active_id() == tab.id()) {
    UpdateToolbar();
  }
  UpdateSpinner();
  InvalidateStrip();
}

void BrowserWindow::OnTabClosed(Tab& tab) {
  const TabId id = tab.id();
  auto it = tabs_.find(id);
  if (it == tabs_.end()) {
    return;
  }
  if (fullscreen_tab_ == id) {
    OnTabFullscreenChanged(tab, false);
  }
  // A tab closed on its own can be reopened; a window closing as a whole
  // is kept as one entry (WM_DESTROY). The last tab of a window counts as
  // a tab (Chrome).
  const bool replaced = replaced_tabs_.erase(id) > 0;
  if (const std::optional<std::size_t> index = model_.IndexOf(id);
      index && !replaced && !closing_ && kind_ == Kind::kNormal &&
      !discarded_profiles_.contains(tab.profile())) {
    owner_.OnTabClosedToReopen(ClosedTabFor(tab, *index));
  }
  model_.RemoveTab(id);
  // Deleted from a task: this is called from inside the tab.
  std::shared_ptr<Tab> closed(it->second.release());
  tabs_.erase(it);
  closed->SetVisible(false);
  CefPostTask(TID_UI, new FunctionTask([closed] {}));
  if (tabs_.empty()) {
    // The last tab closes its window, as in Chrome.
    closing_ = true;
    PostMessageW(window_, WM_CLOSE, 0, 0);
    return;
  }
  ModelChanged();
  if (!closing_) {
    FocusActiveTab();
  }
}

std::optional<PopupHost> BrowserWindow::OnTabPopup(
    Tab& opener,
    const PopupRequest& request) {
  // Popups that were created are no longer pending.
  std::erase_if(pending_popups_, [this](const auto& entry) {
    BrowserWindow* window = entry.second.first;
    if (!owner_.IsOpenWindow(window)) {
      return true;
    }
    Tab* tab = window->FindTab(entry.second.second);
    return !tab || tab->has_browser();
  });
  // window.open() with size features opens a popup window (Chrome); other
  // popups become tabs next to their opener. Link routing (010) comes with
  // W3.
  BrowserWindow* target = this;
  if (request.wants_popup_window) {
    target = owner_.OpenPopupWindow(request.width > 0 ? request.width : 640,
                                    request.height > 0 ? request.height : 480);
    if (!target) {
      return std::nullopt;
    }
  }
  const bool foreground = request.disposition != CEF_WOD_NEW_BACKGROUND_TAB;
  Tab* tab = target->InsertTabFromOpener(&opener, foreground);
  pending_popups_[{opener.id(), request.popup_id}] = {target, tab->id()};
  return tab->PopupHostForBrowser();
}

void BrowserWindow::OnTabPopupAborted(Tab& opener, int popup_id) {
  auto it = pending_popups_.find({opener.id(), popup_id});
  if (it == pending_popups_.end()) {
    return;
  }
  const auto [window, tab] = it->second;
  pending_popups_.erase(it);
  if (owner_.IsOpenWindow(window)) {
    window->RemoveUncreatedTab(tab);
  }
}

void BrowserWindow::OpenUrlFromTab(Tab& opener,
                                   const std::string& url,
                                   OpenDisposition disposition) {
  if (disposition == OpenDisposition::kNewWindow) {
    owner_.OpenWindow(url, opener.profile());
    return;
  }
  const bool foreground = disposition == OpenDisposition::kNewForegroundTab;
  Tab* tab = InsertTabFromOpener(&opener, foreground);
  tab->CreateBrowser(url, owner_.RequestContext(tab->profile()));
  if (foreground) {
    FocusActiveTab();
  }
}

bool BrowserWindow::OnTabKey(const CefKeyEvent& event) {
  if (event.type != KEYEVENT_RAWKEYDOWN) {
    return false;
  }
  return HandleShortcut(event.windows_key_code,
                        (event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0,
                        (event.modifiers & EVENTFLAG_ALT_DOWN) != 0,
                        (event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0);
}

TabClientDelegate::SearchEngine BrowserWindow::TabSearchEngine() {
  return owner_.SearchEngine();
}

LRESULT BrowserWindow::OnNcCalcSize(WPARAM wparam, LPARAM lparam) {
  if (wparam && fullscreen_) {
    return 0;  // No frame: the client area is the whole window.
  }
  if (!wparam || kind_ == Kind::kPopup) {
    return DefWindowProcW(window_, WM_NCCALCSIZE, wparam, lparam);
  }
  // The system computes the frame with its title bar; the client area then
  // takes the title bar's place, keeping the side and bottom borders.
  auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
  const LONG top = params->rgrc[0].top;
  const LRESULT result = DefWindowProcW(window_, WM_NCCALCSIZE, wparam, lparam);
  params->rgrc[0].top = top;
  if (IsZoomed(window_)) {
    // A maximised window reaches beyond the screen by its border.
    params->rgrc[0].top += ResizeBorderHeight();
  }
  return result;
}

LRESULT BrowserWindow::OnNcHitTest(LPARAM lparam) {
  if (fullscreen_) {
    return HTCLIENT;
  }
  const LRESULT system = DefWindowProcW(window_, WM_NCHITTEST, 0, lparam);
  if (system != HTCLIENT || kind_ == Kind::kPopup) {
    return system;  // Side and bottom borders, or a normal title bar.
  }
  POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
  ScreenToClient(window_, &point);
  RECT client = {};
  GetClientRect(window_, &client);
  if (!IsZoomed(window_)) {
    // The top border is inside our client area.
    const int border = ResizeBorderHeight();
    if (point.y < border) {
      if (point.x < border * 2) {
        return HTTOPLEFT;
      }
      if (point.x >= client.right - border * 2) {
        return HTTOPRIGHT;
      }
      return HTTOP;
    }
  }
  if (point.y < StripBounds().bottom) {
    switch (strip_.HitTest(point).part) {
      case TabStrip::Part::kMinimize:
        return HTMINBUTTON;
      case TabStrip::Part::kMaximize:
        return HTMAXBUTTON;  // Windows 11 shows snap layouts on it.
      case TabStrip::Part::kClose:
        return HTCLOSE;
      case TabStrip::Part::kTab:
      case TabStrip::Part::kTabClose:
      case TabStrip::Part::kNewTab:
      case TabStrip::Part::kGroupLabel:
        return HTCLIENT;
      case TabStrip::Part::kNone:
        return HTCAPTION;  // Drags the window; double-click maximises.
    }
  }
  return HTCLIENT;
}

void BrowserWindow::OnNcButton(UINT message, WPARAM hit_code) {
  TabStrip::Part part = TabStrip::Part::kNone;
  switch (hit_code) {
    case HTMINBUTTON:
      part = TabStrip::Part::kMinimize;
      break;
    case HTMAXBUTTON:
      part = TabStrip::Part::kMaximize;
      break;
    case HTCLOSE:
      part = TabStrip::Part::kClose;
      break;
    default:
      break;
  }
  switch (message) {
    case WM_NCMOUSEMOVE:
      if (hover_.part != part) {
        hover_ = {part, 0};
        InvalidateStrip();
      }
      if (!tracking_mouse_) {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE | TME_NONCLIENT,
                                 window_, 0};
        tracking_mouse_ = TrackMouseEvent(&track) != FALSE;
      }
      break;
    case WM_NCLBUTTONDOWN:
      pressed_ = {part, 0};
      InvalidateStrip();
      break;
    case WM_NCLBUTTONUP:
      if (part != TabStrip::Part::kNone && pressed_.part == part) {
        if (part == TabStrip::Part::kMinimize) {
          ShowWindow(window_, SW_MINIMIZE);
        } else if (part == TabStrip::Part::kMaximize) {
          ShowWindow(window_, IsZoomed(window_) ? SW_RESTORE : SW_MAXIMIZE);
        } else {
          PostMessageW(window_, WM_CLOSE, 0, 0);
        }
      }
      pressed_ = {};
      InvalidateStrip();
      break;
    default:
      break;
  }
}

std::optional<ProfileId> BrowserWindow::DropTarget(POINT point) const {
  if (!drag_tab_ || !dragging_) {
    return std::nullopt;
  }
  const std::optional<ProfileId> group = strip_.GroupAt(point);
  const ProfileId* own = model_.ProfileOf(*drag_tab_);
  if (!group || !own || *group == *own) {
    return std::nullopt;
  }
  return group;
}

void BrowserWindow::MoveTabToProfile(TabId id, const ProfileId& profile) {
  Tab* tab = FindTab(id);
  if (!tab || tab->closing() || tab->profile() == profile) {
    return;
  }
  const std::wstring name = Utf8ToWide(owner_.ProfileLook(profile).name);
  BrowserWindowOwner& owner = owner_;
  // The question's answer comes later: the window may have closed by then.
  tab->AskMoveToProfile(name, [this, &owner, id, profile](bool move) {
    if (!move || !owner.IsOpenWindow(this)) {
      return;
    }
    Tab* moved = FindTab(id);
    if (!moved || moved->closing()) {
      return;
    }
    const std::string url =
        IsBlank(moved->url()) ? std::string() : moved->url();
    OpenTab(url, true, profile);
    replaced_tabs_.insert(id);
    CloseTab(id);
  });
}

void BrowserWindow::OnMouse(UINT message, POINT point) {
  TabStrip::Hit hit = strip_.HitTest(point);
  // A tab dragged over another profile's group: that group's label is
  // highlighted, where the tab goes when dropped.
  if (const std::optional<ProfileId> target = DropTarget(point)) {
    hit = {TabStrip::Part::kGroupLabel, 0, *target};
  }
  switch (message) {
    case WM_MOUSEMOVE: {
      if (!tracking_mouse_) {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window_, 0};
        tracking_mouse_ = TrackMouseEvent(&track) != FALSE;
      }
      if (drag_tab_) {
        // A tab moves once the mouse has moved far enough (the system's
        // drag threshold).
        if (!dragging_ &&
            std::abs(point.x - drag_start_.x) >
                GetSystemMetricsForDpi(SM_CXDRAG, GetDpiForWindow(window_))) {
          dragging_ = true;
        }
        if (dragging_) {
          const std::optional<TabId> target = strip_.TabAtX(point.x);
          const std::optional<std::size_t> index =
              target ? model_.IndexOf(*target) : std::nullopt;
          if (index && target != drag_tab_ &&
              model_.MoveTab(*drag_tab_, *index)) {
            ModelChanged();
          }
        }
      }
      if (!(hover_ == hit)) {
        hover_ = hit;
        InvalidateStrip();
        if (tooltip_) {
          // The next tip asks for its text again (TTN_GETDISPINFO).
          SendMessageW(tooltip_, TTM_POP, 0, 0);
          SendMessageW(tooltip_, TTM_UPDATE, 0, 0);
        }
      }
      break;
    }
    case WM_LBUTTONDOWN:
      if (hit.part == TabStrip::Part::kTab) {
        if (model_.ActivateTab(hit.tab)) {
          ModelChanged();
          FocusActiveTab();
        }
        drag_tab_ = hit.tab;
        drag_start_ = point;
        SetCapture(window_);
      } else if (hit.part == TabStrip::Part::kTabClose ||
                 hit.part == TabStrip::Part::kNewTab ||
                 hit.part == TabStrip::Part::kGroupLabel) {
        pressed_ = hit;
        SetCapture(window_);
        InvalidateStrip();
      }
      break;
    case WM_LBUTTONUP: {
      const TabStrip::Hit pressed = pressed_;
      const std::optional<TabId> dropped = drag_tab_;
      const std::optional<ProfileId> target = DropTarget(point);
      pressed_ = {};
      drag_tab_.reset();
      dragging_ = false;
      if (GetCapture() == window_) {
        ReleaseCapture();
      }
      if (dropped && target) {
        MoveTabToProfile(*dropped, *target);
      } else if (pressed == hit && hit.part == TabStrip::Part::kTabClose) {
        CloseTab(hit.tab);
      } else if (pressed == hit && hit.part == TabStrip::Part::kNewTab) {
        OpenTab(std::string(), true, hit.profile);
      } else if (pressed == hit && hit.part == TabStrip::Part::kGroupLabel) {
        ToggleGroup(hit.profile);
      }
      InvalidateStrip();
      break;
    }
    case WM_RBUTTONUP:
      if (hit.part == TabStrip::Part::kTab ||
          hit.part == TabStrip::Part::kTabClose) {
        POINT screen = point;
        ClientToScreen(window_, &screen);
        ShowTabMenu(hit.tab, screen);
      }
      break;
    case WM_MBUTTONUP:
      // Middle-click closes a tab (Chrome).
      if (hit.part == TabStrip::Part::kTab ||
          hit.part == TabStrip::Part::kTabClose) {
        CloseTab(hit.tab);
      }
      break;
    default:
      break;
  }
}

// static
LRESULT CALLBACK BrowserWindow::WindowProc(HWND window,
                                           UINT message,
                                           WPARAM wparam,
                                           LPARAM lparam) {
  auto* self = reinterpret_cast<BrowserWindow*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// static
LRESULT CALLBACK BrowserWindow::AddressProc(HWND window,
                                            UINT message,
                                            WPARAM wparam,
                                            LPARAM lparam,
                                            UINT_PTR /*id*/,
                                            DWORD_PTR data) {
  auto* self = reinterpret_cast<BrowserWindow*>(data);
  switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      const int key = static_cast<int>(wparam);
      if (self->HandleSuggestionKey(key, KeyDown(VK_SHIFT))) {
        return 0;
      }
      if (key == VK_RETURN) {
        self->NavigateToAddressBarText();
        return 0;
      }
      if (key == VK_ESCAPE) {
        self->ShowAddress();
        SendMessageW(window, EM_SETSEL, 0, -1);
        return 0;
      }
      if (self->HandleShortcut(key, KeyDown(VK_CONTROL), KeyDown(VK_MENU),
                               KeyDown(VK_SHIFT))) {
        return 0;
      }
      break;
    }
    case WM_CHAR:
      // Enter, Escape and Ctrl+Tab are handled above; without this the edit
      // beeps.
      if (wparam == L'\r' || wparam == 0x1B || wparam == L'\t') {
        return 0;
      }
      break;
    case WM_KILLFOCUS:
      self->CloseSuggestions();
      self->typed_text_.clear();
      self->ShowAddress();
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &BrowserWindow::AddressProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT BrowserWindow::HandleMessage(UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam) {
  switch (message) {
    case WM_NCCALCSIZE:
      return OnNcCalcSize(wparam, lparam);
    case WM_NCHITTEST:
      return OnNcHitTest(lparam);
    case WM_NCMOUSEMOVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
      if (kind_ == Kind::kNormal &&
          (wparam == HTMINBUTTON || wparam == HTMAXBUTTON ||
           wparam == HTCLOSE)) {
        OnNcButton(message, wparam);
        return 0;  // The system must not draw its own buttons.
      }
      if (message == WM_NCMOUSEMOVE && hover_.part != TabStrip::Part::kNone) {
        hover_ = {};
        InvalidateStrip();
      }
      break;
    case WM_NCMOUSELEAVE:
    case WM_MOUSELEAVE:
      tracking_mouse_ = false;
      hover_ = {};
      if (message == WM_NCMOUSELEAVE) {
        pressed_ = {};
      }
      InvalidateStrip();
      return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
      OnMouse(message, {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
      return 0;
    case WM_NOTIFY:
      if (const auto* header = reinterpret_cast<const NMHDR*>(lparam);
          tooltip_ && header->hwndFrom == tooltip_ &&
          header->code == TTN_GETDISPINFOW) {
        auto* info = reinterpret_cast<NMTTDISPINFOW*>(lparam);
        tooltip_text_.clear();
        if (hover_.part == TabStrip::Part::kTab) {
          if (const Tab* tab = FindTab(hover_.tab)) {
            tooltip_text_ = Utf8ToWide(tab->DisplayTitle());
          }
        } else if (hover_.part == TabStrip::Part::kTabClose) {
          tooltip_text_ = L"Close tab";
        } else {
          tooltip_text_ = strip_.ToolTip(hover_);
        }
        // An empty text shows no tip.
        info->lpszText = tooltip_text_.data();
        return 0;
      }
      break;
    case WM_CAPTURECHANGED:
      drag_tab_.reset();
      dragging_ = false;
      return 0;
    case WM_ERASEBKGND:
      return 1;  // Painted in WM_PAINT.
    case WM_PAINT:
      Paint();
      return 0;
    case WM_SIZE:
      CloseSuggestions();
      Layout();
      owner_.OnSessionChanged();
      return 0;
    case WM_MOVE:
      CloseSuggestions();
      owner_.OnSessionChanged();
      return 0;
    case WM_ACTIVATE:
      active_ = LOWORD(wparam) != WA_INACTIVE;
      InvalidateStrip();
      if (!active_) {
        CloseSuggestions();
        HWND focus = GetFocus();
        saved_focus_ = focus && IsChild(window_, focus) ? focus : nullptr;
      } else if (HWND focus = GetFocus(); focus && IsChild(window_, focus)) {
        // Activated again while the focus is still inside (the window was
        // already in front): leave it where it is. DefWindowProc would move
        // it to the window and from there to the page.
        return 0;
      }
      break;
    case WM_SETFOCUS:
      // Back to where the user was, as in Chrome. The page itself is
      // focused through its browser.
      if (Tab* tab = ActiveTab(); saved_focus_ && IsWindow(saved_focus_) &&
                                  IsWindowVisible(saved_focus_) &&
                                  IsWindowEnabled(saved_focus_) &&
                                  !(tab && tab->IsPageWindow(saved_focus_))) {
        SetFocus(saved_focus_);
        saved_focus_ = nullptr;
      } else if (tab) {
        tab->Focus();
      }
      return 0;
    case WM_TIMER:
      if (wparam == kFullscreenHintTimer) {
        KillTimer(window_, kFullscreenHintTimer);
        fullscreen_hint_.reset();
        return 0;
      }
      if (wparam == kSpinnerTimer) {
        ++spinner_step_;
        InvalidateStrip();
        return 0;
      }
      break;
    case WM_SETTINGCHANGE:
      // Light or dark app mode changed.
      if (lparam && std::wstring_view(reinterpret_cast<const wchar_t*>(
                        lparam)) == L"ImmersiveColorSet") {
        RefreshTheme();
      }
      break;
    case WM_DPICHANGED: {
      const auto* suggested = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left,
                   suggested->bottom - suggested->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      UpdateFonts();
      for (const auto& [id, tab] : tabs_) {
        tab->UpdateFaviconSize();
      }
      Layout();
      return 0;
    }
    case WM_DRAWITEM:
      if (const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
          item->hwndItem == zoom_ || item->hwndItem == popups_) {
        DrawIndicator(*item);
      } else if (item->hwndItem == downloads_) {
        DrawDownloadsButton(*item);
      } else {
        DrawToolbarButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
      }
      return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {  // A read-only edit asks for "static" colours.
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, theme_.text);
      SetBkColor(dc, theme_.field);
      return reinterpret_cast<LRESULT>(field_brush_);
    }
    case WM_COMMAND:
      if (reinterpret_cast<HWND>(lparam) == address_ &&
          HIWORD(wparam) == EN_CHANGE) {
        OnAddressEdited();
        return 0;
      }
      if (Tab* tab = ActiveTab()) {
        switch (LOWORD(wparam)) {
          case kBackId:
            tab->GoBack();
            return 0;
          case kForwardId:
            tab->GoForward();
            return 0;
          case kReloadId:
            tab->Reload();
            return 0;
          case kZoomId:
            tab->Zoom(CEF_ZOOM_COMMAND_RESET);
            return 0;
          case kPopupsId:
            ShowBlockedPopups();
            return 0;
          case kMenuId:
            ShowAppMenu();
            return 0;
          case kDownloadsId:
            ShowDownloads();
            return 0;
          default:
            break;
        }
      }
      break;
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize.x = Scale(400);
      info->ptMinTrackSize.y = Scale(300);
      return 0;
    }
    case WM_CLOSE: {
      // Each page decides first (unload handlers, "leave site?"); the
      // window closes with its last tab (OnTabClosed).
      if (tabs_.empty()) {
        DestroyWindow(window_);
        return 0;
      }
      if (!closing_) {
        if (!owner_.OnWindowClosing(*this)) {
          return 0;  // The user kept downloads running.
        }
        closed_snapshot_.clear();
        for (std::size_t i = 0; i < model_.count() && is_normal(); ++i) {
          const Tab* tab = FindTab(model_.id_at(i));
          if (tab && !tab->closing() &&
              !discarded_profiles_.contains(tab->profile())) {
            closed_snapshot_.push_back(ClosedTabFor(*tab, i));
          }
        }
      }
      closing_ = true;
      std::vector<TabId> ids;
      for (const auto& [id, tab] : tabs_) {
        ids.push_back(id);
      }
      for (TabId id : ids) {
        CloseTab(id);
      }
      return 0;
    }
    case WM_DESTROY: {
      KillTimer(window_, kSpinnerTimer);
      HWND window = window_;
      window_ = nullptr;
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      if (!closed_snapshot_.empty()) {
        owner_.OnWindowClosedToReopen(window_id_, std::move(closed_snapshot_));
        closed_snapshot_.clear();
      }
      owner_.OnWindowDestroyed(this);
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
