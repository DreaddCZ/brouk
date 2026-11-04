#ifndef BROUK_PLATFORM_WIN_SITE_INFO_POPUP_H_
#define BROUK_PLATFORM_WIN_SITE_INFO_POPUP_H_

#include <windows.h>

#include <string>
#include <vector>

#include "cef/site_info.h"
#include "include/cef_browser.h"
#include "platform/win/theme.h"

namespace brouk::win {

// The popup under the address bar's site icon (012, items 3 and 6), as on
// macOS: the connection's state and the site's address, the certificate
// (the system's certificate dialog), and the site's permissions, which can
// be changed or reset, then "Reload to apply the changes". Escape or
// clicking elsewhere closes it. UI thread.
class SiteInfoPopup {
 public:
  class Delegate {
   public:
    // Reloads the page the popup was opened for.
    virtual void SiteInfoReload() = 0;

   protected:
    ~Delegate() = default;
  };

  explicit SiteInfoPopup(Delegate& delegate);
  ~SiteInfoPopup();
  SiteInfoPopup(const SiteInfoPopup&) = delete;
  SiteInfoPopup& operator=(const SiteInfoPopup&) = delete;

  // Shows the popup for `browser`'s page under `anchor` (screen pixels,
  // the icon), its left edge at the icon's. The fonts are the window's,
  // which outlives the popup.
  void Show(HWND owner,
            const RECT& anchor,
            CefRefPtr<CefBrowser> browser,
            const Theme& theme,
            HFONT font,
            HFONT icon_font);
  void Close();
  bool visible() const { return window_ && IsWindowVisible(window_); }
  // Clicking the icon while the popup is open closes it first (it loses
  // the activation); the click must not open it again.
  bool JustClosed() const { return GetTickCount64() - hidden_at_ < 300; }

  // The icon for the address bar (Segoe Fluent Icons) and its accessible
  // name, for `level`.
  static const wchar_t* Glyph(SecurityLevel level);
  static std::wstring AccessibleName(SecurityLevel level);

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  static LRESULT CALLBACK ControlProc(HWND window,
                                      UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam,
                                      UINT_PTR id,
                                      DWORD_PTR data);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  // Creates the controls from the top; returns the height they need.
  int CreateControls();
  void PermissionChanged(std::size_t index);
  void ResetPermissions();
  void ShowReloadHint();
  void ShowCertificate();
  int Scale(int dips) const;

  Delegate& delegate_;
  HWND owner_ = nullptr;
  HWND window_ = nullptr;
  CefRefPtr<CefBrowser> browser_;
  SiteInfo info_;
  std::vector<SitePermission> permissions_;
  HWND glyph_ = nullptr;  // The connection's icon.
  std::vector<HWND> permission_boxes_;
  HWND reload_hint_ = nullptr;
  HWND reload_ = nullptr;
  Theme theme_;
  HFONT font_ = nullptr;
  HFONT icon_font_ = nullptr;
  HFONT bold_font_ = nullptr;  // Owned.
  HBRUSH background_ = nullptr;
  ULONGLONG hidden_at_ = 0;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_SITE_INFO_POPUP_H_
