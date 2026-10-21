#ifndef BROUK_CEF_TAB_CLIENT_H_
#define BROUK_CEF_TAB_CLIENT_H_

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/dialog_spam_guard.h"
#include "include/cef_client.h"

namespace brouk {

// A popup the page asked for (window.open, target=_blank, Cmd+click).
struct PopupRequest {
  int popup_id = 0;
  std::string target_url;
  cef_window_open_disposition_t disposition = CEF_WOD_UNKNOWN;
  bool user_gesture = false;
  // window.open() with size or position features: Chrome opens these as a
  // popup window instead of a tab.
  bool wants_popup_window = false;
};

// A JavaScript dialog (alert/confirm/prompt) or the "leave site?" dialog.
struct JsDialogRequest {
  enum class Kind { kAlert, kConfirm, kPrompt, kBeforeUnload };
  Kind kind = Kind::kAlert;
  // Origin shown in the title ("example.com says"), formatted for security
  // display by Chromium. Empty for dialogs without an origin.
  std::string origin;
  std::string message;
  std::string default_prompt_text;
  // For kBeforeUnload: the page is being reloaded, not left.
  bool is_reload = false;
  // Show "Prevent this page from creating additional dialogs" (Chrome).
  bool offer_suppression = false;
};

// The user's answer to a JS dialog. `ok` is true for OK/Leave; `input` is the
// prompt text; `suppress_more` is the checkbox state.
using JsDialogDone =
    std::function<void(bool ok, const std::string& input, bool suppress_more)>;

// Where a link from the context menu opens (Chrome's choices).
enum class OpenDisposition { kNewBackgroundTab, kNewForegroundTab, kNewWindow };

// State of a download, for the download notice (012, item 18).
struct DownloadInfo {
  enum class State { kInProgress, kComplete, kCanceled, kFailed };
  std::uint32_t id = 0;
  State state = State::kInProgress;
  std::string full_path;  // Empty until the target path is known.
  int percent = -1;       // -1 if unknown.
};

// Why a tab's renderer process is gone (crash page text depends on it).
enum class RendererGoneReason { kCrashed, kKilled, kOutOfMemory, kOther };

// The answer to "page unresponsive": true to exit (kill) the page, false to
// keep waiting.
using UnresponsiveDone = std::function<void(bool exit_page)>;

// Where the platform UI hosts a popup's browser.
struct PopupHost {
  CefWindowHandle parent_view = kNullWindowHandle;
  CefRect bounds;
  CefRefPtr<CefClient> client;
};

// Implemented by the platform UI for one tab. Every method is called on the
// CEF UI thread (the main thread on macOS).
class TabClientDelegate {
 public:
  virtual ~TabClientDelegate() = default;

  virtual void OnBrowserCreated(CefRefPtr<CefBrowser> browser) = 0;
  virtual void OnTitleChanged(const std::string& title) = 0;
  virtual void OnAddressChanged(const std::string& url) = 0;
  virtual void OnLoadingStateChanged(bool is_loading,
                                     bool can_go_back,
                                     bool can_go_forward) = 0;
  // PNG-encoded favicon of the current page, or empty if there is none.
  virtual void OnFaviconChanged(const std::vector<std::uint8_t>& png) = 0;

  // Returns where to host the popup's browser, or nullopt to block it.
  virtual std::optional<PopupHost> OnBeforePopup(
      const PopupRequest& request) = 0;
  // A popup allowed by OnBeforePopup failed to be created.
  virtual void OnPopupAborted(int popup_id) = 0;

  // The browser is ready to close (unload handlers done). The platform must
  // remove the browser's view from its hierarchy now.
  virtual void OnCloseReady() = 0;
  // The browser is gone; release all references to it.
  virtual void OnBrowserClosed() = 0;

  // Shows a dialog over this tab only (tab-modal). `done` must be called once
  // when the user answers, unless DismissJsDialog comes first.
  virtual void ShowJsDialog(const JsDialogRequest& request,
                            JsDialogDone done) = 0;
  // The page navigated or closed: remove the dialog without an answer.
  virtual void DismissJsDialog() = 0;

  // The page's renderer process crashed or was killed: show the crash page
  // ("Aw, Snap!") with a reload button. Cleared when a new load starts.
  // `error_code` is display text such as "Error code: SIGSEGV", or empty.
  virtual void OnRendererGone(RendererGoneReason reason,
                              const std::string& error_code) = 0;
  // The page stopped responding: ask the user to wait or exit the page.
  virtual void OnRendererUnresponsive(UnresponsiveDone done) = 0;
  // The page responds again: remove the question.
  virtual void OnRendererResponsive() = 0;

  // The page entered or left HTML full screen (video, presentation). The
  // platform makes the window full screen and hides the browser UI.
  virtual void OnFullscreenModeChanged(bool fullscreen) = 0;

  // Find in page progress: `count` matches, `active` is the 1-based index of
  // the highlighted one; `final_update` is false while still searching.
  virtual void OnFindResult(int count, int active, bool final_update) = 0;

  // Context menu actions that need the platform UI.
  virtual void OpenUrl(const std::string& url, OpenDisposition disposition) = 0;
  virtual void CopyTextToClipboard(const std::string& text) = 0;
  virtual void CopyImageToClipboard(const std::vector<std::uint8_t>& png) = 0;

  // Downloads: a unique path in the downloads folder for the suggested
  // (untrusted) file name, reserved until released.
  virtual std::string ReserveDownloadPath(const std::string& suggested) = 0;
  virtual void ReleaseDownloadPath(const std::string& path) = 0;
  virtual void OnDownloadUpdated(const DownloadInfo& info) = 0;

  // A key event the page did not handle, with the native event (NSEvent* on
  // macOS). Return true if the UI handled it (e.g. as a menu shortcut).
  virtual bool OnUnhandledKeyEvent(const CefKeyEvent& event,
                                   CefEventHandle os_event) = 0;
};

// CefClient for one tab's browser. Translates CEF callbacks into calls on a
// TabClientDelegate, so platform code never implements CEF interfaces itself.
class TabClient : public CefClient,
                  public CefContextMenuHandler,
                  public CefDisplayHandler,
                  public CefDownloadHandler,
                  public CefFindHandler,
                  public CefJSDialogHandler,
                  public CefKeyboardHandler,
                  public CefLifeSpanHandler,
                  public CefLoadHandler,
                  public CefRequestHandler {
 public:
  // `delegate` must outlive this client or be detached with DetachDelegate().
  explicit TabClient(TabClientDelegate* delegate) : delegate_(delegate) {}
  TabClient(const TabClient&) = delete;
  TabClient& operator=(const TabClient&) = delete;

  // UI thread. Stops all further callbacks to the delegate.
  void DetachDelegate();

  // CefClient:
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return this;
  }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefFindHandler> GetFindHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

  // CefContextMenuHandler:
  void OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefContextMenuParams> params,
                           CefRefPtr<CefMenuModel> model) override;
  bool OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefContextMenuParams> params,
                            int command_id,
                            EventFlags event_flags) override;

  // CefDisplayHandler:
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                          const std::vector<CefString>& icon_urls) override;
  void OnFullscreenModeChange(CefRefPtr<CefBrowser> browser,
                              bool fullscreen) override;

  // CefDownloadHandler:
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;

  // CefFindHandler:
  void OnFindResult(CefRefPtr<CefBrowser> browser,
                    int identifier,
                    int count,
                    const CefRect& selection_rect,
                    int active_match_ordinal,
                    bool final_update) override;

  // CefJSDialogHandler:
  bool OnJSDialog(CefRefPtr<CefBrowser> browser,
                  const CefString& origin_url,
                  JSDialogType dialog_type,
                  const CefString& message_text,
                  const CefString& default_prompt_text,
                  CefRefPtr<CefJSDialogCallback> callback,
                  bool& suppress_message) override;
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser,
                            const CefString& message_text,
                            bool is_reload,
                            CefRefPtr<CefJSDialogCallback> callback) override;
  void OnResetDialogState(CefRefPtr<CefBrowser> browser) override;

  // CefKeyboardHandler:
  bool OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                     const CefKeyEvent& event,
                     CefEventHandle os_event,
                     bool* is_keyboard_shortcut) override;
  bool OnKeyEvent(CefRefPtr<CefBrowser> browser,
                  const CefKeyEvent& event,
                  CefEventHandle os_event) override;

  // CefLifeSpanHandler:
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     int popup_id,
                     const CefString& target_url,
                     const CefString& target_frame_name,
                     WindowOpenDisposition target_disposition,
                     bool user_gesture,
                     const CefPopupFeatures& popup_features,
                     CefWindowInfo& window_info,
                     CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override;
  void OnBeforePopupAborted(CefRefPtr<CefBrowser> browser,
                            int popup_id) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefLoadHandler:
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool is_loading,
                            bool can_go_back,
                            bool can_go_forward) override;

  // CefRequestHandler:
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override;
  bool OnRenderProcessUnresponsive(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefUnresponsiveProcessCallback> callback) override;
  void OnRenderProcessResponsive(CefRefPtr<CefBrowser> browser) override;

 private:
  void CopyImageAt(const CefRefPtr<CefBrowser>& browser,
                   const std::string& url);

  // Shows a dialog and answers `callback` once the user responds.
  void ShowDialog(const JsDialogRequest& request,
                  const CefRefPtr<CefJSDialogCallback>& callback);

  void DownloadFavicon(const CefRefPtr<CefBrowser>& browser,
                       const std::string& url);

  TabClientDelegate* delegate_;  // Not owned. Null after DetachDelegate().
  std::string favicon_url_;      // URL of the favicon being shown.
  // URLs from "Save Link/Image As…": their downloads ask where to save.
  std::set<std::string> save_as_urls_;
  // Reserved download paths by download id, released when it ends.
  std::map<std::uint32_t, std::string> download_paths_;
  DialogSpamGuard dialog_guard_;
  // The dialog waiting for the user's answer, if any.
  CefRefPtr<CefJSDialogCallback> pending_dialog_;

  IMPLEMENT_REFCOUNTING(TabClient);
};

}  // namespace brouk

#endif  // BROUK_CEF_TAB_CLIENT_H_
