#ifndef BROUK_CEF_TAB_CLIENT_H_
#define BROUK_CEF_TAB_CLIENT_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

  // A key event the page did not handle, with the native event (NSEvent* on
  // macOS). Return true if the UI handled it (e.g. as a menu shortcut).
  virtual bool OnUnhandledKeyEvent(const CefKeyEvent& event,
                                   CefEventHandle os_event) = 0;
};

// CefClient for one tab's browser. Translates CEF callbacks into calls on a
// TabClientDelegate, so platform code never implements CEF interfaces itself.
class TabClient : public CefClient,
                  public CefDisplayHandler,
                  public CefKeyboardHandler,
                  public CefLifeSpanHandler,
                  public CefLoadHandler {
 public:
  // `delegate` must outlive this client or be detached with DetachDelegate().
  explicit TabClient(TabClientDelegate* delegate) : delegate_(delegate) {}
  TabClient(const TabClient&) = delete;
  TabClient& operator=(const TabClient&) = delete;

  // UI thread. Stops all further callbacks to the delegate.
  void DetachDelegate();

  // CefClient:
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

  // CefDisplayHandler:
  void OnAddressChange(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                          const std::vector<CefString>& icon_urls) override;

  // CefKeyboardHandler:
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

 private:
  void DownloadFavicon(const CefRefPtr<CefBrowser>& browser,
                       const std::string& url);

  TabClientDelegate* delegate_;  // Not owned. Null after DetachDelegate().
  std::string favicon_url_;      // URL of the favicon being shown.

  IMPLEMENT_REFCOUNTING(TabClient);
};

}  // namespace brouk

#endif  // BROUK_CEF_TAB_CLIENT_H_
