#ifndef BROUK_PLATFORM_WIN_TAB_H_
#define BROUK_PLATFORM_WIN_TAB_H_

#include <windows.h>

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cef/internal_page_client.h"
#include "cef/tab_client.h"
#include "core/tab_strip_model.h"
#include "include/cef_browser.h"
#include "include/cef_request_context.h"
#include "platform/win/favicon.h"
#include "platform/win/find_bar.h"
#include "platform/win/tab_panel.h"

namespace brouk::win {

class Tab;

// What a tab tells the window it is in. UI thread.
class TabHost {
 public:
  // Title, address, icon, loading or navigation state changed.
  virtual void OnTabChanged(Tab& tab) = 0;
  // The browser is gone (after Close() or a page that closed itself). The
  // host deletes the tab, later (not from inside this call).
  virtual void OnTabClosed(Tab& tab) = 0;
  // The page opens a popup or a link in a new tab: where to put it, or
  // nullopt to block it (TabClientDelegate::OnBeforePopup).
  virtual std::optional<PopupHost> OnTabPopup(Tab& opener,
                                              const PopupRequest& request) = 0;
  virtual void OnTabPopupAborted(Tab& opener, int popup_id) = 0;
  // A context menu or shortcut opens `url` in a new tab or window.
  virtual void OpenUrlFromTab(Tab& opener,
                              const std::string& url,
                              OpenDisposition disposition) = 0;
  // The page entered or left HTML full screen (video, presentation).
  virtual void OnTabFullscreenChanged(Tab& tab, bool fullscreen) = 0;
  // A key the page did not handle (shortcuts). True if handled.
  virtual bool OnTabKey(const CefKeyEvent& event) = 0;
  virtual TabClientDelegate::SearchEngine TabSearchEngine() = 0;
  // The tab icon's size in pixels at the window's current DPI.
  virtual int FaviconSize() = 0;

 protected:
  virtual ~TabHost() = default;
};

class Tab final : public TabClientDelegate,
                  public InternalPageDelegate,
                  public FindBar::Delegate {
 public:
  // Creates the tab's (hidden) container window in `parent`.
  Tab(TabHost& host, TabId id, HWND parent);
  ~Tab() override;
  Tab(const Tab&) = delete;
  Tab& operator=(const Tab&) = delete;

  // Starts the tab's browser with `url`.
  void CreateBrowser(const std::string& url,
                     CefRefPtr<CefRequestContext> context);
  // Starts the tab's browser with an internal page (brouk://..., such as
  // the licences, 023), answered by `handler`, in the internal pages'
  // request context.
  void CreateInternalPage(
      const std::string& url,
      std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler,
      CefRefPtr<CefRequestContext> context);
  // Where CEF creates a popup's browser for this tab instead
  // (TabHost::OnTabPopup).
  PopupHost PopupHostForBrowser();
  void SetBounds(const RECT& bounds);
  void SetVisible(bool visible);
  // The icon again at a new size (the window's DPI changed).
  void UpdateFaviconSize();
  void Navigate(const std::string& url);
  void GoBack();
  void GoForward();
  void Reload();
  // Focuses the page, or a dialog over it.
  void Focus();
  // Find in page (012): opens the find bar and focuses it (Ctrl+F); the
  // next or previous match (F3), or the bar if there is nothing to find.
  void ShowFindBar();
  void FindNext(bool forward);
  // Zoom in, out or back to 100 % (Ctrl+Plus, Ctrl+Minus, Ctrl+0), and the
  // current zoom level (0 is 100 %).
  void Zoom(cef_zoom_command_t command);
  double ZoomLevel() const;
  // The system's print dialog for the page (Ctrl+P).
  void Print();
  // Leaves the page's HTML full screen, if it is in it.
  void ExitFullscreen();
  // Runs the page's unload handlers and closes the browser; OnTabClosed
  // follows. A page may cancel ("leave site?").
  void Close();
  bool has_browser() const { return browser_ != nullptr; }
  CefRefPtr<CefBrowser> browser() const { return browser_; }
  // Popups the popup blocker stopped on the page shown (Chrome's "Pop-ups
  // blocked" in the address bar), and forgetting them.
  const std::vector<std::string>& blocked_popups() const {
    return blocked_popups_;
  }
  void ClearBlockedPopups();
  // Whether `window` is the page's (the browser's window or one inside it).
  bool IsPageWindow(HWND window) const;
  bool closing() const { return closing_; }
  // Shows an internal page (CreateInternalPage), which cannot navigate to
  // web pages.
  bool internal() const { return internal_client_ != nullptr; }

  TabId id() const { return id_; }
  HWND container() const { return container_; }
  const std::string& title() const { return title_; }
  // What the tab strip and the window title show: "New Tab" for an empty
  // tab (Chrome), else the page's title, else its address.
  std::string DisplayTitle() const;
  const std::string& url() const { return url_; }
  const Favicon& favicon() const { return favicon_; }
  bool loading() const { return loading_; }
  bool can_go_back() const { return can_go_back_; }
  bool can_go_forward() const { return can_go_forward_; }

  // TabClientDelegate and InternalPageDelegate:
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override;
  void OnCloseReady() override;
  void OnBrowserClosed() override;
  void OnTitleChanged(const std::string& title) override;
  bool OnUnhandledKeyEvent(const CefKeyEvent& event,
                           CefEventHandle os_event) override;

  // InternalPageDelegate:
  void OnInspectElement(int x, int y) override;

  // FindBar::Delegate:
  void OnFindTextChanged() override;
  void OnFindNext(bool forward) override;
  void OnFindClose() override;
  bool OnFindShortcut(int key, bool control, bool alt, bool shift) override;

  // TabClientDelegate:
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
  // A permission prompt waiting for its turn (one is shown at a time).
  struct PendingPermission {
    PermissionRequest request;
    PermissionDone done;
  };

  HWND BrowserWindowHandle() const;
  void OnContainerSize();
  // A browser shortcut typed in a panel or the find bar, as in the page.
  bool ForwardShortcut(int key, bool control, bool alt, bool shift);

  // Panels over the page (TabPanel). The dialogs are tab-modal: while one
  // is open the page gets no input.
  std::unique_ptr<TabPanel> MakePanel(TabPanel::Content content,
                                      TabPanel::Done done);
  TabPanel* ModalPanel() const;
  // Whether keyboard focus is in this tab (its page or a panel), or nowhere.
  bool FocusInTab() const;
  // After a panel came or went: blocks the page while a dialog is open, and
  // moves focus to the dialog or back to the page if `refocus`.
  void UpdatePanels(bool refocus);
  void ClosePanel(std::unique_ptr<TabPanel>& panel);
  void ShowNextPermissionPrompt();

  static LRESULT CALLBACK ContainerProc(HWND window,
                                        UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam);

  TabHost& host_;
  const TabId id_;
  HWND container_ = nullptr;
  CefRefPtr<TabClient> client_;
  // Instead of client_, for an internal page.
  CefRefPtr<InternalPageClient> internal_client_;
  CefRefPtr<CefBrowser> browser_;
  std::string title_;
  std::string url_;
  std::vector<std::uint8_t> favicon_png_;
  Favicon favicon_;
  bool loading_ = false;
  bool can_go_back_ = false;
  bool can_go_forward_ = false;
  // A navigation asked for before the browser exists.
  std::string pending_url_;
  std::unique_ptr<TabPanel> js_dialog_;
  // The dialog came while no page was loading: a new navigation cancels it.
  bool js_dialog_while_idle_ = false;
  std::unique_ptr<TabPanel> unresponsive_;
  std::unique_ptr<TabPanel> external_protocol_;
  std::unique_ptr<TabPanel> sad_tab_;
  // The first one is shown in permission_prompt_.
  std::deque<PendingPermission> permission_requests_;
  std::unique_ptr<TabPanel> permission_prompt_;
  std::unique_ptr<FindBar> find_bar_;
  std::vector<std::string> blocked_popups_;
  bool closing_ = false;
  // Expires with the tab, for tasks posted while it is open.
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_TAB_H_
