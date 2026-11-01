#include "platform/win/adblock_requests_window.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <array>
#include <map>
#include <utility>

#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukAdblockRequests";

bool RegisterWindowClass() {
  static const bool registered = [] {
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

}  // namespace

AdblockRequestsWindow::AdblockRequestsWindow(
    HWND owner,
    const std::string& host,
    const std::vector<AdblockRequestInfo>& requests,
    const std::function<std::string(const std::string&)>& list_name,
    std::function<void()> closed)
    : closed_(std::move(closed)) {
  if (!RegisterWindowClass()) {
    return;
  }
  const int dpi = static_cast<int>(GetDpiForWindow(owner));
  auto scale = [dpi](int dips) { return MulDiv(dips, dpi, 96); };
  const std::wstring title = L"Blocked requests — " + Utf8ToWide(host);
  window_ = CreateWindowExW(
      0, kWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      CW_USEDEFAULT, CW_USEDEFAULT, scale(900), scale(420), owner, nullptr,
      GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(
      window_, GWLP_WNDPROC,
      reinterpret_cast<LONG_PTR>(&AdblockRequestsWindow::WindowProc));
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                             0, static_cast<UINT>(dpi));
  font_ = CreateFontIndirectW(&metrics.lfMessageFont);
  list_ = CreateWindowExW(
      0, WC_LISTVIEWW, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0,
      0, 0, window_, nullptr, GetModuleHandleW(nullptr), nullptr);
  SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
  ListView_SetExtendedListViewStyle(
      list_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
  constexpr std::array<std::pair<const wchar_t*, int>, 5> kColumns = {{
      {L"Result", 80},
      {L"Type", 90},
      {L"Request", 360},
      {L"Rule", 220},
      {L"List", 130},
  }};
  for (std::size_t i = 0; i < kColumns.size(); ++i) {
    LVCOLUMNW column = {};
    column.mask = LVCF_WIDTH | LVCF_TEXT;
    column.cx = scale(kColumns[i].second);
    column.pszText = const_cast<wchar_t*>(kColumns[i].first);
    ListView_InsertColumn(list_, static_cast<int>(i), &column);
  }
  std::map<std::string, std::wstring> names;
  int row = 0;
  for (const AdblockRequestInfo& info : requests) {
    if (!info.blocked && !info.excepted) {
      continue;
    }
    auto name = names.find(info.list_id);
    if (name == names.end()) {
      name = names.emplace(info.list_id, Utf8ToWide(list_name(info.list_id)))
                 .first;
    }
    std::wstring result = info.blocked ? L"Blocked" : L"Allowed";
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.pszText = result.data();
    ListView_InsertItem(list_, &item);
    std::array<std::wstring, 4> cells = {Utf8ToWide(info.type),
                                         Utf8ToWide(info.url),
                                         Utf8ToWide(info.rule), name->second};
    for (std::size_t i = 0; i < cells.size(); ++i) {
      ListView_SetItemText(list_, row, static_cast<int>(i) + 1,
                           cells[i].data());
    }
    ++row;
  }
  ShowWindow(window_, SW_SHOW);
  SetFocus(list_);
}

AdblockRequestsWindow::~AdblockRequestsWindow() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  if (font_) {
    DeleteObject(font_);
  }
}

LRESULT CALLBACK AdblockRequestsWindow::WindowProc(HWND window,
                                                   UINT message,
                                                   WPARAM wparam,
                                                   LPARAM lparam) {
  auto* self = reinterpret_cast<AdblockRequestsWindow*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  switch (message) {
    case WM_SIZE:
      if (self->list_) {
        SetWindowPos(self->list_, nullptr, 0, 0, LOWORD(lparam), HIWORD(lparam),
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      return 0;
    case WM_SETFOCUS:
      if (self->list_) {
        SetFocus(self->list_);
      }
      return 0;
    case WM_CLOSE:
      ShowWindow(window, SW_HIDE);
      if (self->closed_) {
        // May delete this window later (posted).
        std::function<void()> closed = std::move(self->closed_);
        self->closed_ = nullptr;
        closed();
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace brouk::win
