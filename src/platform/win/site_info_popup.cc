#include "platform/win/site_info_popup.h"

#include <commctrl.h>
#include <cryptuiapi.h>
#include <uxtheme.h>
#include <wincrypt.h>

#include <memory>
#include <string>

#include "core/permissions.h"
#include "core/security_state.h"
#include "platform/win/controls.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukSiteInfoPopup";
constexpr int kWidth = 320;
constexpr int kMargin = 16;
constexpr int kLineHeight = 20;
constexpr int kGap = 8;
constexpr int kButtonHeight = 26;
constexpr int kNameWidth = 130;

// The permission choices, in PermissionSetting's order.
constexpr const wchar_t* kSettings[] = {L"Ask (default)", L"Allow", L"Block"};

enum ControlId : int {
  kCertificateId = 1,
  kResetId,
  kReloadId,
  kFirstPermissionId = 100,
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

// Red for certificate errors, as on macOS.
COLORREF DangerColor(const Theme& theme) {
  return theme.dark ? RGB(0xFF, 0x7B, 0x72) : RGB(0xC4, 0x2B, 0x1C);
}

struct StoreCloser {
  void operator()(HCERTSTORE store) const { CertCloseStore(store, 0); }
};
struct CertificateFreer {
  void operator()(PCCERT_CONTEXT context) const {
    CertFreeCertificateContext(context);
  }
};

}  // namespace

SiteInfoPopup::SiteInfoPopup(Delegate& delegate) : delegate_(delegate) {}

SiteInfoPopup::~SiteInfoPopup() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  if (background_) {
    DeleteObject(background_);
  }
  if (bold_font_) {
    DeleteObject(bold_font_);
  }
}

// static
const wchar_t* SiteInfoPopup::Glyph(SecurityLevel level) {
  switch (level) {
    case SecurityLevel::kSecure:
      return L"";  // Lock.
    case SecurityLevel::kNotSecure:
    case SecurityLevel::kDangerous:
      return L"";  // Warning.
    case SecurityLevel::kNone:
      return L"";  // Info.
  }
  return L"";
}

// static
std::wstring SiteInfoPopup::AccessibleName(SecurityLevel level) {
  return L"Site information: " + Utf8ToWide(SecuritySummary(level));
}

int SiteInfoPopup::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(owner_)), 96);
}

void SiteInfoPopup::Show(HWND owner,
                         const RECT& anchor,
                         CefRefPtr<CefBrowser> browser,
                         const Theme& theme,
                         HFONT font,
                         HFONT icon_font) {
  Close();
  if (!browser || !RegisterWindowClass()) {
    return;
  }
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
    window_ = nullptr;
  }
  owner_ = owner;
  browser_ = browser;
  info_ = GetSiteInfo(browser);
  permissions_ = GetSitePermissions(browser, info_.origin);
  theme_ = theme;
  font_ = font;
  icon_font_ = icon_font;
  if (bold_font_) {
    DeleteObject(bold_font_);
  }
  LOGFONTW bold = {};
  GetObjectW(font_, sizeof(bold), &bold);
  bold.lfWeight = FW_SEMIBOLD;
  bold_font_ = CreateFontIndirectW(&bold);
  if (background_) {
    DeleteObject(background_);
  }
  background_ = CreateSolidBrush(theme_.toolbar);
  window_ =
      CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"Site information",
                      WS_POPUP | WS_BORDER | WS_CLIPCHILDREN, 0, 0, 0, 0,
                      owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(window_, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(&SiteInfoPopup::WindowProc));
  // Sized after the controls, which know their height.
  SetWindowPos(window_, HWND_TOP, anchor.left, anchor.bottom, Scale(kWidth),
               Scale(400), SWP_NOACTIVATE);
  const int height = CreateControls();
  RECT frame = {0, 0, Scale(kWidth), height};
  AdjustWindowRectExForDpi(&frame, WS_POPUP | WS_BORDER, FALSE,
                           WS_EX_TOOLWINDOW, GetDpiForWindow(owner_));
  SetWindowPos(window_, HWND_TOP, anchor.left, anchor.bottom,
               frame.right - frame.left, frame.bottom - frame.top,
               SWP_NOACTIVATE);
  ShowWindow(window_, SW_SHOW);
  if (HWND first = GetNextDlgTabItem(window_, nullptr, FALSE)) {
    SetFocus(first);
  } else {
    SetFocus(window_);
  }
}

int SiteInfoPopup::CreateControls() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  const int margin = Scale(kMargin);
  const int line = Scale(kLineHeight);
  const int gap = Scale(kGap);
  const int button = Scale(kButtonHeight);
  const int inner = Scale(kWidth) - 2 * margin;
  auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style,
                     int x, int y, int w, int h, int id, HFONT font) {
    HWND created = CreateWindowExW(
        0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(created, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    if (theme_.dark) {
      SetWindowTheme(created, L"DarkMode_Explorer", nullptr);
    }
    if (style & WS_TABSTOP) {
      SetWindowSubclass(created, &SiteInfoPopup::ControlProc, 0,
                        reinterpret_cast<DWORD_PTR>(this));
    }
    return created;
  };
  int y = margin;
  // The connection: icon and summary, the address, a warning.
  const int icon = Scale(24);
  glyph_ = control(WC_STATICW, Glyph(info_.level), SS_LEFT | SS_NOPREFIX,
                   margin, y, icon, line, 0, icon_font_);
  control(WC_STATICW, Utf8ToWide(SecuritySummary(info_.level)).c_str(),
          SS_LEFT | SS_NOPREFIX, margin + icon, y, inner - icon, line, 0,
          bold_font_);
  y += line;
  if (!info_.origin.empty()) {
    control(WC_STATICW, Utf8ToWide(info_.origin).c_str(),
            SS_LEFT | SS_NOPREFIX | SS_PATHELLIPSIS, margin, y, inner, line, 0,
            font_);
    y += line;
  }
  if (info_.level == SecurityLevel::kNotSecure) {
    y += gap / 2;
    control(WC_STATICW,
            L"Don't enter passwords or other sensitive information on this "
            L"site.",
            SS_LEFT | SS_NOPREFIX, margin, y, inner, 2 * line, 0, font_);
    y += 2 * line;
  }
  if (!info_.certificate_chain.empty()) {
    y += gap;
    control(WC_BUTTONW, L"Certificate…", WS_TABSTOP | BS_PUSHBUTTON, margin, y,
            Scale(110), button, kCertificateId, font_);
    y += button;
  }
  permission_boxes_.clear();
  reload_hint_ = nullptr;
  reload_ = nullptr;
  if (!info_.origin.empty()) {
    // The site's permissions.
    y += gap;
    control(WC_STATICW, L"", SS_ETCHEDHORZ, margin, y, inner, 1, 0, font_);
    y += gap;
    control(WC_STATICW, L"Permissions", SS_LEFT | SS_NOPREFIX, margin, y, inner,
            line, 0, bold_font_);
    y += line + gap / 2;
    if (permissions_.empty()) {
      control(WC_STATICW, L"This site has no permissions set.",
              SS_LEFT | SS_NOPREFIX, margin, y, inner, line, 0, font_);
      y += line;
    }
    for (std::size_t i = 0; i < permissions_.size(); ++i) {
      const std::wstring name =
          Utf8ToWide(PermissionName(permissions_[i].kind));
      control(WC_STATICW, name.c_str(), SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE,
              margin, y, Scale(kNameWidth), button, 0, font_);
      HWND box =
          control(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                  margin + Scale(kNameWidth), y, inner - Scale(kNameWidth),
                  Scale(200), kFirstPermissionId + static_cast<int>(i), font_);
      for (const wchar_t* setting : kSettings) {
        SendMessageW(box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(setting));
      }
      SendMessageW(box, CB_SETCURSEL,
                   static_cast<WPARAM>(permissions_[i].setting), 0);
      SetAccessibleName(box, name.c_str());
      permission_boxes_.push_back(box);
      y += button + gap / 2;
    }
    if (!permissions_.empty()) {
      y += gap / 2;
      control(WC_BUTTONW, L"Reset permissions", WS_TABSTOP | BS_PUSHBUTTON,
              margin, y, Scale(140), button, kResetId, font_);
      y += button;
    }
    // Shown once a permission changes.
    reload_hint_ = control(WC_STATICW, L"Reload to apply the changes.",
                           SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE, margin,
                           y + gap, inner - Scale(90), button, 0, font_);
    reload_ = control(WC_BUTTONW, L"Reload", WS_TABSTOP | BS_PUSHBUTTON,
                      margin + inner - Scale(80), y + gap, Scale(80), button,
                      kReloadId, font_);
    ShowWindow(reload_hint_, SW_HIDE);
    ShowWindow(reload_, SW_HIDE);
    y += gap + button;
  }
  return y + margin;
}

void SiteInfoPopup::PermissionChanged(std::size_t index) {
  if (index >= permissions_.size() || !browser_) {
    return;
  }
  const LRESULT selected =
      SendMessageW(permission_boxes_[index], CB_GETCURSEL, 0, 0);
  if (selected < 0 || selected > 2) {
    return;
  }
  const auto setting = static_cast<PermissionSetting>(selected);
  permissions_[index].setting = setting;
  SetSitePermission(browser_, info_.origin, permissions_[index].kind, setting);
  ShowReloadHint();
}

void SiteInfoPopup::ResetPermissions() {
  if (!browser_) {
    return;
  }
  ResetSitePermissions(browser_, info_.origin);
  for (std::size_t i = 0; i < permissions_.size(); ++i) {
    permissions_[i].setting = PermissionSetting::kAsk;
    SendMessageW(permission_boxes_[i], CB_SETCURSEL, 0, 0);
  }
  ShowReloadHint();
}

void SiteInfoPopup::ShowReloadHint() {
  if (reload_hint_ && reload_) {
    ShowWindow(reload_hint_, SW_SHOWNA);
    ShowWindow(reload_, SW_SHOWNA);
  }
}

// The system's certificate dialog, as Chrome's "Certificate is valid" on
// Windows: the site's certificate, with the rest of the chain given to the
// dialog so it can show the path.
void SiteInfoPopup::ShowCertificate() {
  if (info_.certificate_chain.empty()) {
    return;
  }
  const std::unique_ptr<void, StoreCloser> store(CertOpenStore(
      CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr));
  if (!store) {
    return;
  }
  std::unique_ptr<const CERT_CONTEXT, CertificateFreer> leaf;
  for (const std::vector<std::uint8_t>& der : info_.certificate_chain) {
    PCCERT_CONTEXT added = nullptr;
    if (!CertAddEncodedCertificateToStore(
            store.get(), X509_ASN_ENCODING, der.data(),
            static_cast<DWORD>(der.size()), CERT_STORE_ADD_ALWAYS,
            leaf ? nullptr : &added)) {
      continue;
    }
    if (!leaf && added) {
      leaf.reset(added);
    }
  }
  if (!leaf) {
    return;
  }
  const HWND owner = owner_;
  Close();
  HCERTSTORE stores[] = {store.get()};
  CRYPTUI_VIEWCERTIFICATE_STRUCTW view = {};
  view.dwSize = sizeof(view);
  view.hwndParent = owner;
  view.dwFlags = CRYPTUI_DISABLE_EDITPROPERTIES | CRYPTUI_DISABLE_ADDTOSTORE;
  view.pCertContext = leaf.get();
  view.cStores = 1;
  view.rghStores = stores;
  BOOL changed = FALSE;
  CryptUIDlgViewCertificateW(&view, &changed);
}

void SiteInfoPopup::Close() {
  if (window_ && IsWindowVisible(window_)) {
    ShowWindow(window_, SW_HIDE);
    hidden_at_ = GetTickCount64();
  }
}

LRESULT CALLBACK SiteInfoPopup::WindowProc(HWND window,
                                           UINT message,
                                           WPARAM wparam,
                                           LPARAM lparam) {
  auto* self = reinterpret_cast<SiteInfoPopup*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// Tab moves between the controls, Escape closes.
LRESULT CALLBACK SiteInfoPopup::ControlProc(HWND window,
                                            UINT message,
                                            WPARAM wparam,
                                            LPARAM lparam,
                                            UINT_PTR /*id*/,
                                            DWORD_PTR data) {
  auto* self = reinterpret_cast<SiteInfoPopup*>(data);
  switch (message) {
    case WM_GETDLGCODE:
      if (wparam == VK_TAB || wparam == VK_ESCAPE) {
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
      if (wparam == VK_ESCAPE &&
          !SendMessageW(window, CB_GETDROPPEDSTATE, 0, 0)) {
        self->Close();
        return 0;
      }
      break;
    case WM_CHAR:
      if (wparam == L'\t' || wparam == 0x1B) {
        return 0;  // Handled above.
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &SiteInfoPopup::ControlProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT SiteInfoPopup::HandleMessage(UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam) {
  switch (message) {
    case WM_COMMAND: {
      const int id = LOWORD(wparam);
      if (id >= kFirstPermissionId && HIWORD(wparam) == CBN_SELCHANGE) {
        PermissionChanged(static_cast<std::size_t>(id - kFirstPermissionId));
        return 0;
      }
      switch (id) {
        case kCertificateId:
          ShowCertificate();
          return 0;
        case kResetId:
          ResetPermissions();
          return 0;
        case kReloadId:
          Close();
          delegate_.SiteInfoReload();
          return 0;
        default:
          break;
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
      // The warning icon of a certificate error in red.
      const bool danger = reinterpret_cast<HWND>(lparam) == glyph_ &&
                          info_.level == SecurityLevel::kDangerous;
      SetTextColor(dc, danger ? DangerColor(theme_) : theme_.text);
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
