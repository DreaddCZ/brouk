#ifndef BROUK_PLATFORM_WIN_ADBLOCK_POPUP_H_
#define BROUK_PLATFORM_WIN_ADBLOCK_POPUP_H_

#include <windows.h>

#include <string>
#include <vector>

#include "cef/adblock_request_handler.h"
#include "platform/win/theme.h"

namespace brouk::win {

// The ad blocker's popup under the shield button (007), as on macOS: the
// switch for the site, what was blocked (ads, trackers, other), the page's
// third-party domains with Block/Allow rules (on this site or everywhere),
// and Block element…, Show blocked requests, Report a broken site, Blocker
// settings and "?". Escape or clicking elsewhere closes it. UI thread.
class AdblockPopup {
 public:
  // What the popup does through its window.
  class Delegate {
   public:
    virtual void PopupSetAdblockSiteEnabled(bool enabled) = 0;
    // A rule in the lists' syntax, for My filters (the page reloads).
    virtual void PopupAddAdblockRule(const std::string& rule) = 0;
    virtual void PopupShowBlockedRequests() = 0;
    virtual void PopupReportBrokenSite() = 0;
    virtual void PopupStartElementPicker() = 0;
    virtual void PopupOpenAdblockSettings() = 0;
    virtual void PopupOpenAdblockHelp() = 0;

   protected:
    ~Delegate() = default;
  };
  // What it shows: the page's host and what the blocker saw there.
  struct State {
    std::string host;
    std::vector<AdblockRequestInfo> requests;
    bool profile_enabled = false;
    bool site_enabled = false;
  };

  explicit AdblockPopup(Delegate& delegate);
  ~AdblockPopup();
  AdblockPopup(const AdblockPopup&) = delete;
  AdblockPopup& operator=(const AdblockPopup&) = delete;

  // Shows the popup under `anchor` (screen pixels, the button), its right
  // edge at the button's. The font is the window's, which outlives it.
  void Show(HWND owner,
            const RECT& anchor,
            const State& state,
            const Theme& theme,
            HFONT font);
  void Close();
  bool visible() const { return window_ && IsWindowVisible(window_); }
  // Clicking the button while the popup is open closes it first (it loses
  // the activation); the click must not open it again.
  bool JustClosed() const { return GetTickCount64() - hidden_at_ < 300; }

 private:
  struct Domain {
    std::string host;
    int requests = 0;
    int blocked = 0;
  };

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
  void CreateControls(const State& state);
  // The rules menu of the domain in `row`, at the screen point.
  void ShowRuleMenu(int row, POINT at);
  int Scale(int dips) const;

  Delegate& delegate_;
  HWND owner_ = nullptr;
  HWND window_ = nullptr;
  HWND list_ = nullptr;
  Theme theme_;
  HFONT font_ = nullptr;
  HBRUSH background_ = nullptr;
  std::string host_;
  std::vector<Domain> domains_;
  ULONGLONG hidden_at_ = 0;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_ADBLOCK_POPUP_H_
