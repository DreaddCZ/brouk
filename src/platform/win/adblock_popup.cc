#include "platform/win/adblock_popup.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>
#include <map>
#include <utility>

#include "core/adblock_lists.h"
#include "platform/win/controls.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukAdblockPopup";
constexpr int kWidth = 360;
constexpr int kMargin = 14;
constexpr int kLineHeight = 20;
constexpr int kGap = 8;
constexpr int kListRowsMax = 8;
constexpr int kButtonHeight = 26;

enum ControlId : int {
  kSiteId = 1,
  kListId,
  kPickId,
  kRequestsId,
  kReportId,
  kSettingsId,
  kHelpId,
};

enum RuleCommand : UINT {
  kBlockSite = 1,
  kBlockEverywhere,
  kAllowSite,
  kAllowEverywhere,
};

bool RegisterWindowClass() {
  static const bool registered = [] {
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
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

// The kind of a blocking list, for the counts (as on macOS).
enum class Kind { kAds, kTrackers, kOther };
Kind KindOf(const std::string& list_id) {
  const FilterListDef* list = FindBuiltinFilterList(list_id);
  if (list == nullptr) {
    return Kind::kOther;
  }
  switch (list->category) {
    case FilterCategory::kAds:
    case FilterCategory::kRegional:
      return Kind::kAds;
    case FilterCategory::kPrivacy:
      return Kind::kTrackers;
    default:
      return Kind::kOther;
  }
}

// Red for domains with blocked requests, as on macOS.
COLORREF BlockedColor(const Theme& theme) {
  return theme.dark ? RGB(0xFF, 0x7B, 0x72) : RGB(0xC4, 0x2B, 0x1C);
}

}  // namespace

AdblockPopup::AdblockPopup(Delegate& delegate) : delegate_(delegate) {}

AdblockPopup::~AdblockPopup() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  if (background_) {
    DeleteObject(background_);
  }
}

int AdblockPopup::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(owner_)), 96);
}

void AdblockPopup::Show(HWND owner,
                        const RECT& anchor,
                        const State& state,
                        const Theme& theme,
                        HFONT font) {
  Close();
  if (!RegisterWindowClass()) {
    return;
  }
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
    window_ = nullptr;
  }
  owner_ = owner;
  theme_ = theme;
  font_ = font;
  host_ = state.host;
  if (background_) {
    DeleteObject(background_);
  }
  background_ = CreateSolidBrush(theme_.toolbar);
  // The third-party domains, most requests first.
  std::map<std::string, Domain> domains;
  for (const AdblockRequestInfo& info : state.requests) {
    if (info.third_party && !info.host.empty()) {
      Domain& domain = domains[info.host];
      domain.host = info.host;
      ++domain.requests;
      domain.blocked += info.blocked ? 1 : 0;
    }
  }
  domains_.clear();
  for (auto& [host, domain] : domains) {
    domains_.push_back(std::move(domain));
  }
  std::ranges::stable_sort(domains_, [](const Domain& a, const Domain& b) {
    return a.requests > b.requests;
  });
  window_ =
      CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Ad blocking",
                      WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, 0, 0, 0, 0,
                      owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(window_, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(&AdblockPopup::WindowProc));
  const int width = Scale(kWidth);
  const int list_rows =
      std::clamp(static_cast<int>(domains_.size()), 1, kListRowsMax);
  const int height = 2 * Scale(kMargin) + 4 * Scale(kLineHeight) +
                     4 * Scale(kGap) + list_rows * Scale(kLineHeight) +
                     Scale(8) + 2 * (Scale(kButtonHeight) + Scale(kGap)) + 2;
  SetWindowPos(window_, HWND_TOP, anchor.right - width, anchor.bottom, width,
               height, SWP_NOACTIVATE);
  CreateControls(state);
  ShowWindow(window_, SW_SHOW);
  SetFocus(GetDlgItem(window_, kSiteId));
}

void AdblockPopup::CreateControls(const State& state) {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  RECT client = {};
  GetClientRect(window_, &client);
  const int margin = Scale(kMargin);
  const int line = Scale(kLineHeight);
  const int gap = Scale(kGap);
  const int inner = client.right - 2 * margin;
  auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style,
                     int x, int y, int w, int h, int id) {
    HWND created = CreateWindowExW(
        0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(created, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    if (theme_.dark) {
      SetWindowTheme(created, L"DarkMode_Explorer", nullptr);
    }
    if (style & WS_TABSTOP) {
      SetWindowSubclass(created, &AdblockPopup::ControlProc, 0,
                        reinterpret_cast<DWORD_PTR>(this));
    }
    return created;
  };
  int y = margin;
  // The switch for the site.
  const std::wstring site = host_.empty() ? L"—" : Utf8ToWide(host_);
  HWND toggle = control(WC_BUTTONW, site.c_str(),
                        WS_TABSTOP | BS_AUTOCHECKBOX | BS_NOTIFY, margin, y,
                        inner, line, kSiteId);
  SendMessageW(toggle, BM_SETCHECK,
               state.site_enabled ? BST_CHECKED : BST_UNCHECKED, 0);
  EnableWindow(toggle, state.profile_enabled && !host_.empty());
  SetAccessibleName(toggle, L"Block ads and trackers on this site");
  y += line;
  const wchar_t* status =
      !state.profile_enabled
          ? L"Blocking is off for this profile (Blocker settings)."
      : state.site_enabled ? L"Ads and trackers are blocked on this site."
                           : L"Blocking is off for this site.";
  control(WC_STATICW, status, SS_LEFT | SS_NOPREFIX, margin, y, inner, line, 0);
  y += line + gap;
  // What was blocked.
  int ads = 0;
  int trackers = 0;
  int other = 0;
  for (const AdblockRequestInfo& info : state.requests) {
    if (!info.blocked) {
      continue;
    }
    switch (KindOf(info.list_id)) {
      case Kind::kAds:
        ++ads;
        break;
      case Kind::kTrackers:
        ++trackers;
        break;
      case Kind::kOther:
        ++other;
        break;
    }
  }
  const std::wstring counts = L"Blocked: " + std::to_wstring(ads) + L" ads · " +
                              std::to_wstring(trackers) + L" trackers · " +
                              std::to_wstring(other) + L" other";
  control(WC_STATICW, counts.c_str(), SS_LEFT | SS_NOPREFIX, margin, y, inner,
          line, 0);
  y += line + gap;
  const std::wstring title = L"Third-party domains (" +
                             std::to_wstring(domains_.size()) +
                             L") — click one for a rule";
  control(WC_STATICW, title.c_str(), SS_LEFT | SS_NOPREFIX, margin, y, inner,
          line, 0);
  y += line;
  // The domains; clicking one (or Enter, the menu key) offers its rules.
  const int list_rows =
      std::clamp(static_cast<int>(domains_.size()), 1, kListRowsMax);
  list_ = control(WC_LISTVIEWW, L"",
                  WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER |
                      LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS,
                  margin, y, inner, list_rows * line + Scale(8), kListId);
  ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT);
  ListView_SetBkColor(list_, theme_.toolbar);
  ListView_SetTextBkColor(list_, theme_.toolbar);
  ListView_SetTextColor(list_, theme_.text);
  SetAccessibleName(list_, L"Third-party domains");
  LVCOLUMNW column = {};
  column.mask = LVCF_WIDTH | LVCF_TEXT;
  column.cx = inner - Scale(110);
  column.pszText = const_cast<wchar_t*>(L"Domain");
  ListView_InsertColumn(list_, 0, &column);
  column.cx = Scale(90);
  column.pszText = const_cast<wchar_t*>(L"Requests");
  ListView_InsertColumn(list_, 1, &column);
  if (domains_.empty()) {
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.pszText =
        const_cast<wchar_t*>(L"No third-party requests on this page.");
    ListView_InsertItem(list_, &item);
    EnableWindow(list_, FALSE);
  }
  for (std::size_t i = 0; i < domains_.size(); ++i) {
    const Domain& domain = domains_[i];
    std::wstring name = Utf8ToWide(domain.host);
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = static_cast<int>(i);
    item.pszText = name.data();
    ListView_InsertItem(list_, &item);
    std::wstring numbers =
        domain.blocked > 0 ? std::to_wstring(domain.requests) + L" (" +
                                 std::to_wstring(domain.blocked) + L" blocked)"
                           : std::to_wstring(domain.requests);
    ListView_SetItemText(list_, static_cast<int>(i), 1, numbers.data());
  }
  y += list_rows * line + Scale(8) + gap;
  // The links, two rows of buttons.
  const int button = Scale(kButtonHeight);
  const int half = (inner - Scale(kGap)) / 2;
  HWND pick = control(WC_BUTTONW, L"Block element…", WS_TABSTOP | BS_PUSHBUTTON,
                      margin, y, half, button, kPickId);
  EnableWindow(pick,
               state.profile_enabled && state.site_enabled && !host_.empty());
  control(WC_BUTTONW, L"Show blocked requests", WS_TABSTOP | BS_PUSHBUTTON,
          margin + half + Scale(kGap), y, half, button, kRequestsId);
  y += button + gap;
  const int help = button;
  const int third = (inner - help - 2 * Scale(kGap)) / 2;
  control(WC_BUTTONW, L"Report a broken site", WS_TABSTOP | BS_PUSHBUTTON,
          margin, y, third, button, kReportId);
  control(WC_BUTTONW, L"Blocker settings", WS_TABSTOP | BS_PUSHBUTTON,
          margin + third + Scale(kGap), y, third, button, kSettingsId);
  HWND help_button =
      control(WC_BUTTONW, L"?", WS_TABSTOP | BS_PUSHBUTTON,
              client.right - margin - help, y, help, button, kHelpId);
  SetAccessibleName(help_button, L"Help about the ad blocker");
}

void AdblockPopup::ShowRuleMenu(int row, POINT at) {
  if (row < 0 || row >= static_cast<int>(domains_.size())) {
    return;
  }
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kBlockSite, L"Block on this site");
  AppendMenuW(menu, MF_STRING, kBlockEverywhere, L"Block everywhere");
  AppendMenuW(menu, MF_STRING, kAllowSite, L"Allow on this site");
  AppendMenuW(menu, MF_STRING, kAllowEverywhere, L"Allow everywhere");
  const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                      at.x, at.y, 0, window_, nullptr);
  DestroyMenu(menu);
  if (command == 0) {
    return;
  }
  // Domain rules (007), written to My filters in the lists' syntax.
  const std::string& domain = domains_[static_cast<std::size_t>(row)].host;
  std::string rule;
  switch (command) {
    case kBlockEverywhere:
      rule = "||" + domain + "^$important";
      break;
    case kBlockSite:
      rule = "||" + domain + "^$important,domain=" + host_;
      break;
    case kAllowEverywhere:
      rule = "@@||" + domain + "^";
      break;
    default:
      rule = "@@||" + domain + "^$domain=" + host_;
      break;
  }
  Close();
  delegate_.PopupAddAdblockRule(rule);
}

void AdblockPopup::Close() {
  if (window_ && IsWindowVisible(window_)) {
    ShowWindow(window_, SW_HIDE);
    hidden_at_ = GetTickCount64();
  }
}

LRESULT CALLBACK AdblockPopup::WindowProc(HWND window,
                                          UINT message,
                                          WPARAM wparam,
                                          LPARAM lparam) {
  auto* self =
      reinterpret_cast<AdblockPopup*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// Tab moves between the controls, Escape closes, Enter or the menu key on
// a domain offers its rules.
LRESULT CALLBACK AdblockPopup::ControlProc(HWND window,
                                           UINT message,
                                           WPARAM wparam,
                                           LPARAM lparam,
                                           UINT_PTR /*id*/,
                                           DWORD_PTR data) {
  auto* self = reinterpret_cast<AdblockPopup*>(data);
  switch (message) {
    case WM_GETDLGCODE:
      if (wparam == VK_TAB || wparam == VK_ESCAPE ||
          (wparam == VK_RETURN && window == self->list_)) {
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
      if (wparam == VK_ESCAPE) {
        self->Close();
        return 0;
      }
      if (window == self->list_ && (wparam == VK_RETURN || wparam == VK_APPS ||
                                    (wparam == VK_F10 && KeyDown(VK_SHIFT)))) {
        const int row = ListView_GetNextItem(window, -1, LVNI_SELECTED);
        RECT item = {};
        if (row >= 0 && ListView_GetItemRect(window, row, &item, LVIR_LABEL)) {
          POINT at = {item.left, item.bottom};
          ClientToScreen(window, &at);
          self->ShowRuleMenu(row, at);
        }
        return 0;
      }
      break;
    case WM_CHAR:
      if (wparam == L'\t' || wparam == 0x1B ||
          (wparam == L'\r' && window == self->list_)) {
        return 0;  // Handled above.
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &AdblockPopup::ControlProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT AdblockPopup::HandleMessage(UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam) {
  switch (message) {
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kSiteId:
          if (HIWORD(wparam) == BN_CLICKED) {
            const bool on = SendMessageW(reinterpret_cast<HWND>(lparam),
                                         BM_GETCHECK, 0, 0) == BST_CHECKED;
            Close();
            delegate_.PopupSetAdblockSiteEnabled(on);
          }
          return 0;
        case kPickId:
          Close();
          delegate_.PopupStartElementPicker();
          return 0;
        case kRequestsId:
          Close();
          delegate_.PopupShowBlockedRequests();
          return 0;
        case kReportId:
          Close();
          delegate_.PopupReportBrokenSite();
          return 0;
        case kSettingsId:
          Close();
          delegate_.PopupOpenAdblockSettings();
          return 0;
        case kHelpId:
          Close();
          delegate_.PopupOpenAdblockHelp();
          return 0;
        default:
          break;
      }
      break;
    case WM_NOTIFY: {
      const auto* header = reinterpret_cast<const NMHDR*>(lparam);
      if (header->hwndFrom != list_) {
        break;
      }
      if (header->code == NM_CLICK || header->code == NM_RCLICK) {
        const auto* click = reinterpret_cast<const NMITEMACTIVATE*>(lparam);
        if (click->iItem >= 0) {
          POINT at = click->ptAction;
          ClientToScreen(list_, &at);
          ShowRuleMenu(click->iItem, at);
        }
        return 0;
      }
      if (header->code == NM_CUSTOMDRAW) {
        auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lparam);
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) {
          return CDRF_NOTIFYITEMDRAW;
        }
        if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
          const auto row = static_cast<std::size_t>(draw->nmcd.dwItemSpec);
          draw->clrText = row < domains_.size() && domains_[row].blocked > 0
                              ? BlockedColor(theme_)
                              : theme_.text;
          return CDRF_DODEFAULT;
        }
      }
      break;
    }
    case WM_ACTIVATE:
      if (LOWORD(wparam) == WA_INACTIVE) {
        Close();  // Clicking elsewhere, as Chrome's bubbles.
      }
      break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, theme_.text);
      SetBkColor(dc, theme_.toolbar);
      return reinterpret_cast<LRESULT>(background_);
    }
    case WM_ERASEBKGND: {
      RECT client = {};
      GetClientRect(window_, &client);
      FillRect(reinterpret_cast<HDC>(wparam), &client, background_);
      return 1;
    }
    case WM_CLOSE:
      Close();
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
