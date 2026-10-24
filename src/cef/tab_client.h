#ifndef BROUK_CEF_TAB_CLIENT_H_
#define BROUK_CEF_TAB_CLIENT_H_

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "cef/adblock_request_handler.h"
#include "core/dialog_spam_guard.h"
#include "core/permissions.h"
#include "core/screen_share.h"
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
  // Requested content size (window.open features), 0 if not given.
  int width = 0;
  int height = 0;
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

// A site asks for permissions (camera, location, ...).
struct PermissionRequest {
  std::uint64_t id = 0;
  std::string origin;  // For display, e.g. "meet.google.com".
  std::vector<PermissionKind> kinds;
};
using PermissionDone = std::function<void(PermissionChoice choice)>;

// A link wants to open another app (mailto:, msteams:, ...).
struct ExternalProtocolRequest {
  std::string url;            // Untrusted, from the page.
  std::string scheme;         // "mailto"
  std::string origin;         // Display form of the page asking; may be empty.
  bool can_remember = false;  // Offer "Always allow" (the page has an origin).
};
// `open`: open the app now; `always`: remember for the site and scheme.
using ExternalProtocolDone = std::function<void(bool open, bool always)>;

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
  // A page was shown in the main frame (history, 013): after loading,
  // without an error, at its final URL after redirects; also for
  // same-document navigations (history.pushState). `transition` is CEF's
  // source and qualifiers.
  virtual void OnPageVisited(const std::string& url,
                             const std::string& title,
                             cef_transition_type_t transition) = 0;
  virtual void OnLoadingStateChanged(bool is_loading,
                                     bool can_go_back,
                                     bool can_go_forward) = 0;
  // PNG-encoded favicon of the current page, or empty if there is none.
  virtual void OnFaviconChanged(const std::vector<std::uint8_t>& png) = 0;

  // Returns where to host the popup's browser, or nullopt to block it.
  virtual std::optional<PopupHost> OnBeforePopup(
      const PopupRequest& request) = 0;
  // A popup without user gesture was blocked (Chrome's popup blocker); the
  // address bar offers to open it or allow popups for the site.
  virtual void OnPopupBlocked(const std::string& target_url) = 0;
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

  // Shows a permission prompt for this tab; `done` is called once with the
  // user's choice, unless DismissPermissionPrompt comes first.
  virtual void ShowPermissionPrompt(const PermissionRequest& request,
                                    PermissionDone done) = 0;
  // The request went away (navigation, tab closed): remove its prompt.
  virtual void DismissPermissionPrompt(std::uint64_t id) = 0;

  // Asks whether to open another app for a link; `done` is called once.
  virtual void AskOpenExternalProtocol(const ExternalProtocolRequest& request,
                                       ExternalProtocolDone done) = 0;
  // Opens the URL with the app macOS (or the OS) registered for it.
  virtual void OpenExternalUrl(const std::string& url) = 0;

  // The page entered or left HTML full screen (video, presentation). The
  // platform makes the window full screen and hides the browser UI.
  virtual void OnFullscreenModeChanged(bool fullscreen) = 0;

  // Whether the page plays sound (reported by the page script; an estimate).
  virtual void OnAudibleChanged(bool audible) = 0;

  // The page asked to share a screen or window: show the picker. `done` gets
  // the user's choice, or nullopt if cancelled or no picker can be shown;
  // it is called once.
  using DesktopSourceDone = std::function<void(std::optional<DesktopSource>)>;
  virtual void ChooseDesktopSource(DesktopSourceDone done) = 0;

  // Find in page progress: `count` matches, `active` is the 1-based index of
  // the highlighted one; `final_update` is false while still searching.
  virtual void OnFindResult(int count, int active, bool final_update) = 0;

  // Context menu actions that need the platform UI.
  virtual void OpenUrl(const std::string& url, OpenDisposition disposition) = 0;
  virtual void CopyTextToClipboard(const std::string& text) = 0;
  virtual void CopyImageToClipboard(const std::vector<std::uint8_t>& png) = 0;

  // Settings (004) the tab follows.
  struct SearchEngine {
    std::string name;          // "Google", for "Search Google for …".
    std::string url_template;  // With %s for the query.
  };
  virtual SearchEngine CurrentSearchEngine() = 0;
  // "Inspect" in the context menu, at page coordinates (006).
  virtual void InspectElement(int x, int y) = 0;
  // Every download asks where to save (otherwise only "Save As…").
  virtual bool AskWhereToSaveDownloads() = 0;

  // Downloads: a unique path in the downloads folder for the suggested
  // (untrusted) file name, reserved until released.
  virtual std::string ReserveDownloadPath(const std::string& suggested) = 0;
  virtual void ReleaseDownloadPath(const std::string& path) = 0;
  virtual void OnDownloadUpdated(const DownloadInfo& info) = 0;

  // Ad blocking (007): a request the ad blocker matched or that went to a
  // third party; and a page load it blocked (the tab explains it).
  virtual void OnAdblockRequest(const AdblockRequestInfo& info) = 0;
  virtual void OnPageBlocked(const AdblockRequestInfo& info) = 0;

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
                  public CefPermissionHandler,
                  public CefRequestHandler {
 public:
  // `delegate` must outlive this client or be detached with DetachDelegate().
  explicit TabClient(TabClientDelegate* delegate) : delegate_(delegate) {}
  TabClient(const TabClient&) = delete;
  TabClient& operator=(const TabClient&) = delete;

  // UI thread. Stops all further callbacks to the delegate.
  void DetachDelegate();

  // Ad blocking for the tab's requests (007), set before its browser is
  // created. `service` outlives every browser.
  void SetAdblock(const AdblockService* service, ProfileId profile) {
    adblock_ = service;
    adblock_profile_ = std::move(profile);
  }

  // The settings a tab's renderer follows (kRendererConfigMessage), sent
  // when they change.
  static CefRefPtr<CefDictionaryValue> RendererConfig(bool json_viewer);
  static void SendRendererConfig(const CefRefPtr<CefBrowser>& browser,
                                 bool json_viewer);

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
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override {
    return this;
  }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

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
  void OnLoadStart(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   TransitionType transition_type) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser,
                            bool is_loading,
                            bool can_go_back,
                            bool can_go_forward) override;
  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int http_status_code) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode error_code,
                   const CefString& error_text,
                   const CefString& failed_url) override;

  // CefPermissionHandler:
  bool OnRequestMediaAccessPermission(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      const CefString& requesting_origin,
      uint32_t requested_permissions,
      CefRefPtr<CefMediaAccessCallback> callback) override;
  bool OnShowPermissionPrompt(
      CefRefPtr<CefBrowser> browser,
      uint64_t prompt_id,
      const CefString& requesting_origin,
      uint32_t requested_permissions,
      CefRefPtr<CefPermissionPromptCallback> callback) override;
  void OnDismissPermissionPrompt(
      CefRefPtr<CefBrowser> browser,
      uint64_t prompt_id,
      cef_permission_request_result_t result) override;

  // CefRequestHandler:
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        const CefString& target_url,
                        WindowOpenDisposition target_disposition,
                        bool user_gesture) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override;
  bool OnRenderProcessUnresponsive(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefUnresponsiveProcessCallback> callback) override;
  void OnRenderProcessResponsive(CefRefPtr<CefBrowser> browser) override;
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool is_navigation,
      bool is_download,
      const CefString& request_initiator,
      bool& disable_default_handling) override;

 private:
  const AdblockService* adblock_ = nullptr;  // Read on the IO thread.
  ProfileId adblock_profile_;
  // "Allow this time": a grant that ends when the tab leaves the site.
  struct OneTimeGrant {
    std::string origin;
    PermissionKind kind;
    // Chromium stored it as a site setting, which must be reset afterwards.
    bool stored_in_content_settings;
  };
  void RecordOneTimeGrants(const CefRefPtr<CefBrowser>& browser,
                           const std::string& origin,
                           const std::vector<PermissionKind>& kinds,
                           bool stored_in_content_settings);
  // Ends one-time grants for origins other than `current_origin` (all of
  // them if empty).
  void EndOneTimeGrants(const CefRefPtr<CefBrowser>& browser,
                        const std::string& current_origin);
  bool HasOneTimeGrant(const std::string& origin, PermissionKind kind) const;

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
  std::vector<OneTimeGrant> one_time_grants_;
  // Frames whose page script reported audio playing.
  std::set<std::string> audible_frames_;
  // For history: whether the tab is loading (a document), and whether the
  // main frame's current load failed (an error page). UI thread.
  bool loading_ = false;
  bool main_load_failed_ = false;
  // Reports the visible navigation entry as a visit.
  void ReportVisit(const CefRefPtr<CefBrowser>& browser);
  void SetFrameAudible(const std::string& frame_id, bool audible);
  // An external-protocol question is open (one at a time, like Chrome).
  bool asking_external_protocol_ = false;
  // Screen sharing picker: one at a time; choosing grants the capture.
  void ChooseDesktopSourceForFrame(const CefRefPtr<CefFrame>& frame,
                                   std::uint32_t request_id);
  bool choosing_desktop_source_ = false;
  ScreenShareGrant screen_share_grant_;
  // Ids for media prompts (CEF's own prompt ids start at 1 and grow).
  std::uint64_t next_media_prompt_id_ = 1ULL << 62;
  DialogSpamGuard dialog_guard_;
  // The dialog waiting for the user's answer, if any.
  CefRefPtr<CefJSDialogCallback> pending_dialog_;

  IMPLEMENT_REFCOUNTING(TabClient);
};

}  // namespace brouk

#endif  // BROUK_CEF_TAB_CLIENT_H_
