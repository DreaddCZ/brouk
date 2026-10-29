#include "platform/win/browser_window.h"

#include <commctrl.h>
#include <windowsx.h>
// Defines the accessibility GUIDs of oleacc.h here (no library has them).
#include <initguid.h>
#include <oleacc.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <vector>

#include "brouk/app_info.h"
#include "cef/function_task.h"
#include "core/address_input.h"
#include "core/internal_pages.h"
#include "core/profile_id.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
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

enum ControlId : int {
  kBackId = 1,
  kForwardId,
  kReloadId,
  kMenuId,
  kAddressId,
};

// Items of the "⋮" menu.
enum MenuCommand : UINT {
  kNewTabCommand = 100,
  kNewWindowCommand,
  kHelpCommand,
  kLicencesCommand,
  kExitCommand,
};

// Glyphs of the "Segoe MDL2 Assets" icon font, on Windows 10 and 11.
constexpr wchar_t kBackGlyph[] = L"\uE72B";
constexpr wchar_t kForwardGlyph[] = L"\uE72A";
constexpr wchar_t kReloadGlyph[] = L"\uE72C";
constexpr wchar_t kMoreGlyph[] = L"\uE712";

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

std::wstring WindowText(HWND window) {
  const int length = GetWindowTextLengthW(window);
  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(window, text.data(), length + 1);
  text.resize(static_cast<size_t>(length));
  return text;
}

bool KeyDown(int key) {
  return (GetKeyState(key) & 0x8000) != 0;
}

// The name screen readers announce for a control whose text is an icon
// glyph (MSAA annotation, also read through UI Automation).
void SetAccessibleName(HWND control, const wchar_t* name) {
  IAccPropServices* services = nullptr;
  if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr,
                              CLSCTX_INPROC_SERVER, IID_IAccPropServices,
                              reinterpret_cast<void**>(&services)))) {
    return;
  }
  services->SetHwndPropStr(control, static_cast<DWORD>(OBJID_CLIENT),
                           CHILDID_SELF, PROPID_ACC_NAME, name);
  services->Release();
}

bool IsBlank(const std::string& url) {
  return url.empty() || url == "about:blank";
}

}  // namespace

BrowserWindow::BrowserWindow(BrowserWindowOwner& owner, Kind kind)
    : owner_(owner), kind_(kind), model_(ProfileId(kFirstProfileId)) {}

BrowserWindow::~BrowserWindow() {
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
  ShowWindow(window_, SW_SHOWNORMAL);
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
  if (kind_ == Kind::kPopup) {
    return {0, 0, client.right, 0};
  }
  return {0, 0, client.right,
          std::min(static_cast<int>(client.bottom), Scale(kStripHeight))};
}

RECT BrowserWindow::ToolbarBounds() const {
  RECT client = {};
  GetClientRect(window_, &client);
  const RECT strip = StripBounds();
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
  strip_.Layout(StripBounds(), GetDpiForWindow(window_), model_,
                kind_ == Kind::kNormal);
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
  }
  const int field_height = Scale(kAddressHeight);
  const int field_top = toolbar.top + (toolbar_height - field_height) / 2;
  address_field_ = {x, field_top, std::max(x, right - margin),
                    field_top + field_height};
  const int padding = Scale(kAddressPadding);
  const int edit_height = text_height_ + Scale(2);
  SetWindowPos(
      address_, nullptr, address_field_.left + padding,
      field_top + (field_height - edit_height) / 2,
      std::max(0, static_cast<int>(address_field_.right - address_field_.left) -
                      padding * 2),
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

void BrowserWindow::ShowAppMenu() {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kNewTabCommand, L"New tab\tCtrl+T");
  AppendMenuW(menu, MF_STRING, kNewWindowCommand, L"New window\tCtrl+N");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kHelpCommand, L"Brouk Help\tF1");
  AppendMenuW(menu, MF_STRING, kLicencesCommand, L"Open-source licences");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit");
  RECT button = {};
  GetWindowRect(menu_, &button);
  const UINT command = static_cast<UINT>(TrackPopupMenuEx(
      menu, TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
      button.right, button.bottom, window_, nullptr));
  DestroyMenu(menu);
  switch (command) {
    case kNewTabCommand:
      OpenTab(std::string(), true);
      break;
    case kNewWindowCommand:
      owner_.OpenWindow(std::string());
      break;
    case kHelpCommand:
      owner_.OpenHelp(std::string());
      break;
    case kLicencesCommand:
      OpenInternalPage("brouk://credits/");
      break;
    case kExitCommand:
      owner_.CloseAllWindows();
      break;
    default:
      break;
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
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler =
      owner_.InternalPageHandler(*page);
  if (!handler) {
    return;  // Not on Windows yet.
  }
  // In place of an empty tab, as Chrome loads chrome:// pages there.
  Tab* active = ActiveTab();
  const bool replace = active && IsBlank(active->url()) && !active->internal();
  Tab* tab = InsertTabFromOpener(active, true);
  tab->CreateInternalPage(url, std::move(handler), owner_.InternalContext());
  if (replace) {
    CloseTab(active->id());
  }
  FocusActiveTab();
}

Tab* BrowserWindow::FindTab(TabId id) {
  auto it = tabs_.find(id);
  return it == tabs_.end() ? nullptr : it->second.get();
}

Tab* BrowserWindow::ActiveTab() {
  const std::optional<TabId> active = model_.active_id();
  return active ? FindTab(*active) : nullptr;
}

void BrowserWindow::OpenTab(const std::string& url, bool foreground) {
  if (IsInternalPageUrl(url)) {
    // From the command line or a link: brouk:// pages load only in their
    // own request context.
    if (std::optional<std::string> page = InternalPageOf(url)) {
      if (*page == "help") {
        owner_.OpenHelp(std::string());
        return;
      }
      if (auto handler = owner_.InternalPageHandler(*page)) {
        const TabId id = NextTabId();
        auto tab = std::make_unique<Tab>(*this, id, window_);
        tab->SetBounds(ContentBounds());
        Tab* created = tab.get();
        tabs_[id] = std::move(tab);
        model_.AppendTab(id, foreground);
        created->CreateInternalPage(url, std::move(handler),
                                    owner_.InternalContext());
        ModelChanged();
        return;
      }
    }
  }
  const TabId id = NextTabId();
  auto tab = std::make_unique<Tab>(*this, id, window_);
  tab->SetBounds(ContentBounds());
  Tab* created = tab.get();
  tabs_[id] = std::move(tab);
  model_.AppendTab(id, foreground);
  created->CreateBrowser(url.empty() ? std::string("about:blank") : url,
                         owner_.RequestContext());
  ModelChanged();
  if (foreground) {
    FocusActiveTab();
  }
}

Tab* BrowserWindow::InsertTabFromOpener(const Tab* opener, bool foreground) {
  const TabId id = NextTabId();
  auto tab = std::make_unique<Tab>(*this, id, window_);
  tab->SetBounds(ContentBounds());
  Tab* created = tab.get();
  tabs_[id] = std::move(tab);
  if (opener && model_.IndexOf(opener->id())) {
    model_.InsertFromOpener(id, opener->id(), foreground);
  } else {
    model_.AppendTab(id, foreground);
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
  const std::optional<TabId> active = model_.active_id();
  for (const auto& [id, tab] : tabs_) {
    tab->SetVisible(active == id);
  }
  strip_.Layout(StripBounds(), GetDpiForWindow(window_), model_,
                kind_ == Kind::kNormal);
  UpdateToolbar();
  UpdateSpinner();
  InvalidateStrip();
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
  SetWindowTextW(address_, Utf8ToWide(shown).c_str());
}

void BrowserWindow::NavigateToAddressBarText() {
  Tab* tab = ActiveTab();
  if (!tab || kind_ == Kind::kPopup) {
    return;
  }
  const TabClientDelegate::SearchEngine engine = owner_.SearchEngine();
  const AddressInputResult input = ClassifyAddressInput(
      WideToUtf8(WindowText(address_)), engine.url_template);
  if (input.kind == AddressInputResult::Kind::kEmpty) {
    return;
  }
  if (IsInternalPageUrl(input.url)) {
    OpenInternalPage(input.url);
    return;
  }
  if (tab->internal()) {
    // An internal page's tab cannot load web pages: a web tab takes its
    // place.
    const TabId internal = tab->id();
    Tab* web = InsertTabFromOpener(tab, true);
    web->CreateBrowser(input.url, owner_.RequestContext());
    CloseTab(internal);
    FocusActiveTab();
    return;
  }
  tab->Navigate(input.url);
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
  if (kind_ == Kind::kPopup) {
    return false;
  }
  if (control && !alt && !shift && key == 'T') {
    OpenTab(std::string(), true);
    return true;
  }
  if (control && !alt && !shift && key == 'N') {
    owner_.OpenWindow(std::string());
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
    owner_.OpenWindow(url);
    return;
  }
  const bool foreground = disposition == OpenDisposition::kNewForegroundTab;
  Tab* tab = InsertTabFromOpener(&opener, foreground);
  tab->CreateBrowser(url, owner_.RequestContext());
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

void BrowserWindow::OnMouse(UINT message, POINT point) {
  const TabStrip::Hit hit = strip_.HitTest(point);
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
                 hit.part == TabStrip::Part::kNewTab) {
        pressed_ = hit;
        SetCapture(window_);
        InvalidateStrip();
      }
      break;
    case WM_LBUTTONUP: {
      const TabStrip::Hit pressed = pressed_;
      pressed_ = {};
      drag_tab_.reset();
      dragging_ = false;
      if (GetCapture() == window_) {
        ReleaseCapture();
      }
      if (pressed == hit && hit.part == TabStrip::Part::kTabClose) {
        CloseTab(hit.tab);
      } else if (pressed == hit && hit.part == TabStrip::Part::kNewTab) {
        OpenTab(std::string(), true);
      }
      InvalidateStrip();
      break;
    }
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
      OnMouse(message, {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
      return 0;
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
      Layout();
      return 0;
    case WM_ACTIVATE:
      active_ = LOWORD(wparam) != WA_INACTIVE;
      if (!active_) {
        address_had_focus_ = GetFocus() == address_;
      }
      InvalidateStrip();
      break;
    case WM_SETFOCUS:
      // Back to where the user was: the address bar or the page.
      if (address_had_focus_) {
        SetFocus(address_);
      } else if (Tab* tab = ActiveTab()) {
        tab->Focus();
      }
      return 0;
    case WM_TIMER:
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
        theme_ = CurrentTheme();
        ApplyThemeToFrame(window_, theme_);
        UpdateFonts();
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN);
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
      DrawToolbarButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
      return TRUE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {  // A read-only edit asks for "static" colours.
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, theme_.text);
      SetBkColor(dc, theme_.field);
      return reinterpret_cast<LRESULT>(field_brush_);
    }
    case WM_COMMAND:
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
          case kMenuId:
            ShowAppMenu();
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
      owner_.OnWindowDestroyed(this);
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
