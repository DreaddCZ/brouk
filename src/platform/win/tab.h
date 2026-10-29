#ifndef BROUK_PLATFORM_WIN_TAB_H_
#define BROUK_PLATFORM_WIN_TAB_H_

#include <windows.h>

#include <memory>
#include <string>

#include "cef/tab_client.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"

namespace brouk::win {

// What a tab tells the window it is in. UI thread.
class TabHost {
 public:
  virtual void OnTabTitleChanged(const std::string& title) = 0;
  virtual void OnTabAddressChanged(const std::string& url) = 0;
  virtual void OnTabLoadingStateChanged(bool can_go_back,
                                        bool can_go_forward) = 0;
  // The browser is gone (after Close() or a page that closed itself).
  virtual void OnTabClosed() = 0;
  // A page or link asks to open `url` elsewhere; W1 opens it in a new
  // window until the tab strip exists.
  virtual void OpenInNewWindow(const std::string& url) = 0;
  // A key the page did not handle (shortcuts). True if handled.
  virtual bool OnTabKey(const CefKeyEvent& event) = 0;
  virtual TabClientDelegate::SearchEngine TabSearchEngine() = 0;

 protected:
  virtual ~TabHost() = default;
};

// One tab: a CEF browser (Alloy style) in a child window of the browser
// window, and the TabClientDelegate for it. Every method runs on the UI
// thread (the main thread: CefRunMessageLoop).
//
// Features not yet ported to Windows (024, stages W1–W3) answer as "not
// available": permission and protocol prompts are declined and popups
// without a click are blocked. Password, autofill, download and ad blocking
// callbacks never come, as the tab's client has none of those services.
class Tab final : public TabClientDelegate {
 public:
  explicit Tab(TabHost& host);
  ~Tab() override;
  Tab(const Tab&) = delete;
  Tab& operator=(const Tab&) = delete;

  void CreateBrowser(HWND parent,
                     const RECT& bounds,
                     const std::string& url,
                     CefRefPtr<CefRequestContext> context);
  void SetBounds(const RECT& bounds);
  void Navigate(const std::string& url);
  void GoBack();
  void GoForward();
  void Reload();
  void Focus();
  // Runs the page's unload handlers and closes the browser; OnTabClosed
  // follows. A page may cancel ("leave site?").
  void Close();
  bool has_browser() const { return browser_ != nullptr; }

  // TabClientDelegate:
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  void OnTitleChanged(const std::string& title) override;
  void OnAddressChanged(const std::string& url) override;
  void OnPageVisited(const std::string& url,
                     const std::string& title,
                     cef_transition_type_t transition) override;
  void OnLoadingStateChanged(bool is_loading,
                             bool can_go_back,
                             bool can_go_forward) override;
  void OnFaviconChanged(const std::string& icon_url,
                        const std::vector<std::uint8_t>& png) override;
  std::optional<PopupHost> OnBeforePopup(const PopupRequest& request) override;
  void OnPopupBlocked(const std::string& target_url) override;
  void OnPopupAborted(int popup_id) override;
  void OnCloseReady() override;
  void OnBrowserClosed() override;
  void ShowJsDialog(const JsDialogRequest& request, JsDialogDone done) override;
  void DismissJsDialog() override;
  void OnRendererGone(RendererGoneReason reason,
                      const std::string& error_code) override;
  void OnRendererUnresponsive(UnresponsiveDone done) override;
  void OnRendererResponsive() override;
  void ShowPermissionPrompt(const PermissionRequest& request,
                            PermissionDone done) override;
  void DismissPermissionPrompt(std::uint64_t id) override;
  void AskOpenExternalProtocol(const ExternalProtocolRequest& request,
                               ExternalProtocolDone done) override;
  void OpenExternalUrl(const std::string& url) override;
  void OnFullscreenModeChanged(bool fullscreen) override;
  void OnAudibleChanged(bool audible) override;
  void OnMediaAccessChanged(bool video, bool audio) override;
  void ChooseDesktopSource(DesktopSourceDone done) override;
  void OnFindResult(int count, int active, bool final_update) override;
  void OpenUrl(const std::string& url, OpenDisposition disposition) override;
  bool RouteFirstNavigation(const std::string& url) override;
  void CopyImageToClipboard(const std::vector<std::uint8_t>& png) override;
  SearchEngine CurrentSearchEngine() override;
  void InspectElement(int x, int y) override;
  bool AskWhereToSaveDownloads() override;
  DownloadTarget ReserveDownloadPath(const std::string& suggested) override;
  void ReleaseDownloadPath(const std::string& path) override;
  void OnAdblockRequest(const AdblockRequestInfo& info) override;
  void OnPageBlocked(const AdblockRequestInfo& info) override;
  void OnElementPicked(const std::string& url,
                       const std::string& selector) override;
  bool OnUnhandledKeyEvent(const CefKeyEvent& event,
                           CefEventHandle os_event) override;
  bool KeepsFocusOnNavigation() override;

  // PasswordTabDelegate:
  void ShowPasswordMenu(const PasswordMenu& menu,
                        PasswordMenuDone done) override;
  void HidePasswordMenu() override;
  void ShowPasswordPrompt(const PasswordPrompt& prompt,
                          PasswordPromptDone done) override;
  void OnPasswordIconChanged(PasswordIcon icon) override;
  void OpenPasswordsPage() override;
  void CopyTextToClipboard(const std::string& text) override;
  bool HandlePasswordMenuKey(int windows_key_code) override;
  void DecodeQrCodes(std::vector<std::uint8_t> png, QrDone done) override;
  void ShowPasswordMessage(const std::string& title,
                           const std::string& text) override;
  void AskBitwardenPassword(const BitwardenPasswordRequest& request,
                            BitwardenPasswordSubmit submit,
                            std::function<void()> cancelled) override;
  void ConfirmWithTouchId(const std::string& reason, TouchIdDone done) override;

  // AutofillTabDelegate:
  void ShowAutofillPrompt(const AutofillPrompt& prompt,
                          AutofillPromptDone done) override;
  void OpenAutofillSettings() override;

 private:
  HWND BrowserWindowHandle() const;

  TabHost& host_;
  CefRefPtr<TabClient> client_;
  CefRefPtr<CefBrowser> browser_;
  RECT bounds_{};
  // A navigation asked for before the browser exists.
  std::string pending_url_;
  // The answer to an open JavaScript dialog, until it is shown.
  JsDialogDone dialog_done_;
  bool closing_ = false;
  // Expires with the tab, for tasks posted while it is open.
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_TAB_H_
