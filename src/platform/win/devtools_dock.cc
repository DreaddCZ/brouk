#include "platform/win/devtools_dock.h"

#include <windowsx.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <utility>

#include "cef/function_task.h"
#include "core/devtools_frontend.h"
#include "core/text_file.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/controls.h"
#include "platform/win/file_dialogs.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukDevToolsDock";
constexpr wchar_t kContentClass[] = L"BroukDevToolsContent";
constexpr int kHeaderHeight = 28;
constexpr int kDividerWidth = 5;

// The dock's tabs: Chromium DevTools, then our own tools (brouk://tools/#id),
// as on macOS.
struct DockTool {
  const char* id;
  const wchar_t* title;
};
constexpr std::array<DockTool, 5> kDockTools = {{
    {"devtools", L"DevTools"},
    {"site-data", L"Site data"},
    {"requests", L"Requests"},
    {"tokens", L"Tokens"},
    {"rules", L"Rules"},
}};

// The position buttons, right to left after the close button. Glyphs of the
// "Segoe MDL2 Assets" icon font.
struct PositionButton {
  const char* id;
  const wchar_t* glyph;
};
constexpr std::array<PositionButton, 4> kPositions = {{
    {"window", L""},  // OpenInNewWindow.
    {"right", L""},   // DockRight.
    {"left", L""},    // DockLeft.
    {"bottom", L""},  // DockBottom.
}};
constexpr wchar_t kCloseGlyph[] = L"";  // ChromeClose.

bool RegisterWindowClasses() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    if (RegisterClassExW(&window_class) == 0) {
      return false;
    }
    // The browsers' parents: they fill them.
    window_class.lpszClassName = kContentClass;
    window_class.hbrBackground =
        static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

std::wstring PositionName(const std::string& id) {
  if (id == "bottom") {
    return L"Dock to bottom";
  }
  if (id == "left") {
    return L"Dock to left";
  }
  if (id == "right") {
    return L"Dock to right";
  }
  return L"Open in a separate window";
}

// Puts a browser's window over the whole of its parent.
void FillParent(const CefRefPtr<CefBrowser>& browser, HWND parent) {
  if (!browser) {
    return;
  }
  RECT bounds = {};
  GetClientRect(parent, &bounds);
  SetWindowPos(browser->GetHost()->GetWindowHandle(), nullptr, 0, 0,
               bounds.right, bounds.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
}

}  // namespace

DevToolsDock::DevToolsDock(HWND parent,
                           CefRefPtr<CefBrowser> inspected,
                           std::optional<POINT> inspect,
                           CefRefPtr<CefRequestContext> context,
                           DevToolsPreferences* preferences,
                           bool own_tools,
                           Delegate& delegate)
    : delegate_(delegate),
      own_tools_(own_tools),
      context_(std::move(context)),
      preferences_(preferences),
      theme_(CurrentTheme()) {
  CEF_REQUIRE_UI_THREAD();
  if (!RegisterWindowClasses()) {
    frontend_creating_ = false;
    return;
  }
  window_ = CreateWindowExW(0, kWindowClass, L"Developer tools",
                            WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    frontend_creating_ = false;
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(window_, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(&DevToolsDock::WindowProc));
  SetAccessibleName(window_, L"Developer tools");
  UpdateFonts();
  auto content = [this] {
    return CreateWindowExW(0, kContentClass, L"", WS_CHILD | WS_CLIPCHILDREN, 0,
                           0, 0, 0, window_, nullptr, GetModuleHandleW(nullptr),
                           nullptr);
  };
  content_ = content();
  ShowWindow(content_, SW_SHOWNA);
  if (own_tools_) {
    tools_content_ = content();
  }
  channel_ = new DevToolsChannel(inspected);
  if (own_tools_) {
    // Dev tools on: record the tab's requests (006).
    channel_->StartRecording();
    channel_->WatchServiceWorkers();
  }
  client_ = new DevToolsFrontendClient(this, inspected, preferences_);
  if (inspect) {
    client_->InspectAt(inspect->x, inspect->y);
  }
  Layout();
  RECT bounds = {};
  GetClientRect(content_, &bounds);
  CefWindowInfo info;
  info.SetAsChild(content_, CefRect(0, 0, bounds.right, bounds.bottom));
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserHost::CreateBrowser(info, client_,
                                std::string(kDevToolsFrontendUrl),
                                CefBrowserSettings(), nullptr, context_);
}

DevToolsDock::~DevToolsDock() {
  if (client_) {
    client_->DetachDelegate();
  }
  if (tools_client_) {
    tools_client_->DetachDelegate();
  }
  if (window_ && IsWindow(window_)) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  for (HFONT font : {text_font_, icon_font_}) {
    if (font) {
      DeleteObject(font);
    }
  }
}

int DevToolsDock::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(window_)), 96);
}

void DevToolsDock::UpdateFonts() {
  for (HFONT font : {text_font_, icon_font_}) {
    if (font) {
      DeleteObject(font);
    }
  }
  const UINT dpi = GetDpiForWindow(window_);
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                             0, dpi);
  text_font_ = CreateFontIndirectW(&metrics.lfMessageFont);
  LOGFONTW icon = {};
  icon.lfHeight = -MulDiv(11, static_cast<int>(dpi), 96);
  wcscpy_s(icon.lfFaceName, L"Segoe MDL2 Assets");
  icon_font_ = CreateFontIndirectW(&icon);
}

void DevToolsDock::SetParentWindow(HWND parent) {
  if (window_) {
    SetParent(window_, parent);
  }
}

void DevToolsDock::SetPosition(const std::string& position) {
  position_ = position;
  Layout();
  InvalidateRect(window_, nullptr, FALSE);
}

RECT DevToolsDock::DividerRect() const {
  RECT client = {};
  GetClientRect(window_, &client);
  const int width =
      MulDiv(kDividerWidth, static_cast<int>(GetDpiForWindow(window_)), 96);
  if (position_ == "bottom") {
    return {0, 0, client.right, width};
  }
  if (position_ == "right") {
    return {0, 0, width, client.bottom};
  }
  if (position_ == "left") {
    return {client.right - width, 0, client.right, client.bottom};
  }
  return {};
}

void DevToolsDock::Layout() {
  if (!window_) {
    return;
  }
  RECT client = {};
  GetClientRect(window_, &client);
  RECT inner = client;
  const RECT divider = DividerRect();
  if (position_ == "bottom") {
    inner.top = divider.bottom;
  } else if (position_ == "right") {
    inner.left = divider.right;
  } else if (position_ == "left") {
    inner.right = divider.left;
  }
  parts_.clear();
  if (own_tools_) {
    const int header = Scale(kHeaderHeight);
    HDC dc = GetDC(window_);
    HGDIOBJ previous = SelectObject(dc, text_font_);
    int x = inner.left + Scale(8);
    for (const DockTool& tool : kDockTools) {
      SIZE size = {};
      GetTextExtentPoint32W(dc, tool.title, lstrlenW(tool.title), &size);
      const int width = static_cast<int>(size.cx) + Scale(16);
      parts_.push_back({.rect = {x, inner.top, x + width, inner.top + header},
                        .id = tool.id,
                        .text = tool.title});
      x += width;
    }
    SelectObject(dc, previous);
    ReleaseDC(window_, dc);
    int right = inner.right - Scale(4);
    const int button = Scale(26);
    parts_.push_back(
        {.rect = {right - button, inner.top, right, inner.top + header},
         .id = "close",
         .text = kCloseGlyph,
         .glyph = true});
    right -= button + Scale(8);
    for (const PositionButton& item : kPositions) {
      parts_.push_back(
          {.rect = {right - button, inner.top, right, inner.top + header},
           .id = item.id,
           .text = item.glyph,
           .glyph = true});
      right -= button;
    }
    inner.top += header;
  }
  for (HWND content : {content_, tools_content_}) {
    if (content) {
      SetWindowPos(content, nullptr, inner.left, inner.top,
                   std::max(0, static_cast<int>(inner.right - inner.left)),
                   std::max(0, static_cast<int>(inner.bottom - inner.top)),
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }
  FillParent(frontend_, content_);
  if (tools_content_) {
    FillParent(tools_, tools_content_);
  }
}

void DevToolsDock::Paint() {
  PAINTSTRUCT paint = {};
  HDC dc = BeginPaint(window_, &paint);
  RECT client = {};
  GetClientRect(window_, &client);
  SetDCBrushColor(dc, theme_.toolbar);
  FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  // The divider: a line on the page's side, wider to grab.
  if (const RECT divider = DividerRect(); divider.right > divider.left) {
    RECT line = divider;
    if (position_ == "bottom") {
      line.bottom = line.top + 1;
    } else if (position_ == "right") {
      line.right = line.left + 1;
    } else {
      line.left = line.right - 1;
    }
    SetDCBrushColor(dc, theme_.separator);
    FillRect(dc, &line, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  }
  SetBkMode(dc, TRANSPARENT);
  for (const Part& part : parts_) {
    const bool selected = part.id == tool_ || part.id == position_;
    SelectObject(dc, part.glyph ? icon_font_ : text_font_);
    SetTextColor(dc, selected && part.glyph ? GetSysColor(COLOR_HIGHLIGHT)
                     : selected             ? theme_.text
                                            : theme_.text_dim);
    RECT area = part.rect;
    DrawTextW(dc, part.text.c_str(), -1, &area,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (selected && !part.glyph) {
      // The chosen tool is underlined, as a tab.
      RECT underline = {part.rect.left + Scale(4), part.rect.bottom - Scale(2),
                        part.rect.right - Scale(4), part.rect.bottom};
      SetDCBrushColor(dc, GetSysColor(COLOR_HIGHLIGHT));
      FillRect(dc, &underline, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
  }
  EndPaint(window_, &paint);
}

void DevToolsDock::InspectAt(POINT point) {
  if (client_) {
    client_->InspectAt(point.x, point.y);
  }
  ShowTool("devtools");
}

void DevToolsDock::ShowPanel(const std::string& panel) {
  if (client_) {
    client_->ShowPanel(panel);
  }
  ShowTool("devtools");
}

void DevToolsDock::EnterInspectElementMode() {
  if (client_) {
    client_->EnterInspectElementMode();
  }
  ShowTool("devtools");
}

void DevToolsDock::SetRequestRules(const RequestRuleSet& rules) {
  if (!channel_ || !own_tools_) {
    return;
  }
  // The dock outlives its channel's callbacks only while open.
  channel_->SetRules(rules, [this] {
    if (!closing_) {
      delegate_.DockRulesAffectedPage();
    }
  });
}

void DevToolsDock::ShowTool(const std::string& tool) {
  if (!own_tools_ || closing_) {
    return;
  }
  tool_ = tool;
  InvalidateRect(window_, nullptr, FALSE);
  const bool devtools = tool == "devtools";
  ShowWindow(content_, devtools ? SW_SHOWNA : SW_HIDE);
  ShowWindow(tools_content_, devtools ? SW_HIDE : SW_SHOWNA);
  if (devtools) {
    return;
  }
  const std::string url = "brouk://tools/#" + tool;
  if (tools_) {
    tools_->GetMainFrame()->LoadURL(url);
    return;
  }
  if (tools_creating_) {
    return;
  }
  tools_creating_ = true;
  tools_client_ = new InternalPageClient(
      this, std::make_unique<DevToolsToolsHandler>(channel_, this));
  RECT bounds = {};
  GetClientRect(tools_content_, &bounds);
  CefWindowInfo info;
  info.SetAsChild(tools_content_, CefRect(0, 0, bounds.right, bounds.bottom));
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserHost::CreateBrowser(info, tools_client_, url, CefBrowserSettings(),
                                nullptr, context_);
}

void DevToolsDock::Close() {
  if (closing_) {
    return;
  }
  closing_ = true;
  if (channel_) {
    channel_->Close();
  }
  if (tools_) {
    tools_->GetHost()->CloseBrowser(true);
  }
  if (frontend_ && client_) {
    client_->Close();
  }
  FinishClosingIfDone();
}

bool DevToolsDock::closed() const {
  return !frontend_ && !frontend_creating_ && !tools_ && !tools_creating_;
}

// Both browsers are gone (or were never created): the dock is closed.
void DevToolsDock::FinishClosingIfDone() {
  if (!closing_ || !closed() || closed_reported_) {
    return;
  }
  closed_reported_ = true;
  delegate_.DockClosed(*this);
}

// Saves exported files (Postman collection, environment) where the user
// chooses; `extra` goes next to the main file.
void DevToolsDock::SaveFiles(File main, std::optional<File> extra, Done done) {
  FileDialogOptions options;
  options.title = L"Export";
  const std::filesystem::path name(Utf8ToWide(main.name));
  options.extension = name.extension().wstring();
  if (!options.extension.empty() && options.extension.front() == L'.') {
    options.extension.erase(options.extension.begin());
  }
  options.type_name =
      options.extension.empty() ? L"File" : options.extension + L" file";
  options.file_name = name.stem().wstring();
  if (extra) {
    options.note =
        L"“" + Utf8ToWide(extra->name) + L"” is saved in the same folder.";
  }
  HWND owner = GetAncestor(window_, GA_ROOT);
  auto files = std::make_shared<std::pair<File, std::optional<File>>>(
      std::move(main), std::move(extra));
  // Asked from a task: the dialog runs its own message loop, which must not
  // run inside the page's request.
  CefPostTask(
      TID_UI, new FunctionTask([options, owner, files, done] {
        std::optional<std::filesystem::path> path = AskSavePath(owner, options);
        if (!path) {
          done(false, "");
          return;
        }
        std::optional<Error> error =
            WriteTextFileAtomically(*path, files->first.content);
        if (!error && files->second) {
          // A plain file name only, next to the main file.
          const std::filesystem::path extra_name =
              std::filesystem::path(Utf8ToWide(files->second->name)).filename();
          error = WriteTextFileAtomically(path->parent_path() / extra_name,
                                          files->second->content);
        }
        done(!error, error ? error->message : std::string());
      }));
}

ToolsHost::Rules DevToolsDock::RequestRules() {
  return closing_ ? Rules{} : delegate_.DockRequestRules();
}

std::optional<Error> DevToolsDock::SetRequestRules(RequestRuleSet rules) {
  if (closing_) {
    return Error{"The developer tools have closed."};
  }
  return delegate_.DockSetRequestRules(std::move(rules));
}

// DevTools browser events (UI thread).

void DevToolsDock::OnDevToolsCreated(CefRefPtr<CefBrowser> frontend) {
  frontend_creating_ = false;
  frontend_ = frontend;
  delegate_.DockBrowserCount(1);
  FillParent(frontend_, content_);
  if (closing_) {
    client_->Close();
  }
}

void DevToolsDock::OnDevToolsCloseReady() {
  // The browser lives in a child window of ours: destroying it completes
  // the close (as for tabs).
  if (frontend_) {
    DestroyWindow(frontend_->GetHost()->GetWindowHandle());
  }
}

void DevToolsDock::OnDevToolsClosed() {
  if (frontend_) {
    frontend_ = nullptr;
    delegate_.DockBrowserCount(-1);
  }
  frontend_creating_ = false;
  // DevTools closing (its own close button, or the tab) closes the dock.
  Close();
  FinishClosingIfDone();
}

bool DevToolsDock::OnDevToolsKeyEvent(const CefKeyEvent& event,
                                      CefEventHandle /*os_event*/) {
  return event.type == KEYEVENT_RAWKEYDOWN && delegate_.DockKey(event);
}

void DevToolsDock::OnDevToolsWantsClose() {
  Close();
}

void DevToolsDock::OpenUrlFromDevTools(const std::string& url) {
  delegate_.DockOpenUrl(url);
}

void DevToolsDock::CopyTextFromDevTools(const std::string& text) {
  SetClipboardText(window_, Utf8ToWide(text));
}

// Our own tools' page.

void DevToolsDock::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  tools_creating_ = false;
  tools_ = browser;
  delegate_.DockBrowserCount(1);
  FillParent(tools_, tools_content_);
  if (closing_) {
    tools_->GetHost()->CloseBrowser(true);
  }
}

void DevToolsDock::OnCloseReady() {
  if (tools_) {
    DestroyWindow(tools_->GetHost()->GetWindowHandle());
  }
}

void DevToolsDock::OnBrowserClosed() {
  if (tools_) {
    tools_ = nullptr;
    delegate_.DockBrowserCount(-1);
  }
  tools_creating_ = false;
  if (tools_client_) {
    tools_client_->DetachDelegate();
    tools_client_ = nullptr;
  }
  FinishClosingIfDone();
}

void DevToolsDock::OnTitleChanged(const std::string& /*title*/) {}

bool DevToolsDock::OnUnhandledKeyEvent(const CefKeyEvent& event,
                                       CefEventHandle /*os_event*/) {
  return event.type == KEYEVENT_RAWKEYDOWN && delegate_.DockKey(event);
}

// "Inspect" in the tools page: DevTools for the page itself (006).
void DevToolsDock::OnInspectElement(int x, int y) {
  if (tools_ && !closing_) {
    delegate_.DockInspectTools(tools_, x, y);
  }
}

LRESULT CALLBACK DevToolsDock::WindowProc(HWND window,
                                          UINT message,
                                          WPARAM wparam,
                                          LPARAM lparam) {
  auto* self =
      reinterpret_cast<DevToolsDock*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT DevToolsDock::HandleMessage(UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam) {
  switch (message) {
    case WM_SIZE:
      Layout();
      InvalidateRect(window_, nullptr, FALSE);
      return 0;
    case WM_DPICHANGED_AFTERPARENT:
      UpdateFonts();
      Layout();
      InvalidateRect(window_, nullptr, FALSE);
      return 0;
    case WM_ERASEBKGND:
      return 1;  // Painted in WM_PAINT.
    case WM_PAINT:
      Paint();
      return 0;
    case WM_SETCURSOR: {
      POINT point = {};
      GetCursorPos(&point);
      ScreenToClient(window_, &point);
      const RECT divider = DividerRect();
      if (PtInRect(&divider, point)) {
        SetCursor(LoadCursorW(nullptr,
                              position_ == "bottom" ? IDC_SIZENS : IDC_SIZEWE));
        return TRUE;
      }
      break;
    }
    case WM_LBUTTONDOWN: {
      const POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      const RECT divider = DividerRect();
      if (PtInRect(&divider, point)) {
        // The divider: the dock's size follows the mouse.
        dragging_ = true;
        GetCursorPos(&drag_start_);
        RECT client = {};
        GetClientRect(window_, &client);
        drag_size_ = position_ == "bottom" ? client.bottom : client.right;
        SetCapture(window_);
        return 0;
      }
      for (const Part& part : parts_) {
        if (!PtInRect(&part.rect, point)) {
          continue;
        }
        if (part.id == "close") {
          Close();
        } else if (part.glyph) {
          delegate_.DockWantsPosition(part.id);
        } else {
          ShowTool(part.id);
        }
        return 0;
      }
      break;
    }
    case WM_MOUSEMOVE:
      if (dragging_) {
        POINT point = {};
        GetCursorPos(&point);
        const int delta = position_ == "bottom"  ? drag_start_.y - point.y
                          : position_ == "right" ? drag_start_.x - point.x
                                                 : point.x - drag_start_.x;
        delegate_.DockResized(drag_size_ + delta, false);
        return 0;
      }
      break;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
      if (dragging_) {
        dragging_ = false;
        if (message == WM_LBUTTONUP) {
          ReleaseCapture();
        }
        RECT client = {};
        GetClientRect(window_, &client);
        delegate_.DockResized(
            position_ == "bottom" ? client.bottom : client.right, true);
        return 0;
      }
      break;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
