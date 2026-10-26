#ifndef BROUK_CEF_INTERNAL_PAGE_CLIENT_H_
#define BROUK_CEF_INTERNAL_PAGE_CLIENT_H_

#include <memory>
#include <string>

#include "include/cef_client.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// Platform side of a window that shows one internal page. UI thread.
class InternalPageDelegate {
 public:
  virtual ~InternalPageDelegate() = default;
  virtual void OnBrowserCreated(CefRefPtr<CefBrowser> browser) = 0;
  // Remove the browser's view to complete closing (see DoClose).
  virtual void OnCloseReady() = 0;
  virtual void OnBrowserClosed() = 0;
  virtual void OnTitleChanged(const std::string& title) = 0;
  // Keys the page did not use (menu shortcuts like Cmd+W).
  virtual bool OnUnhandledKeyEvent(const CefKeyEvent& event,
                                   CefEventHandle os_event) = 0;
  // "Inspect" in the page's context menu, at page coordinates (006).
  virtual void OnInspectElement(int x, int y) = 0;
};

// CefClient for a window showing an internal page (brouk://...). Answers the
// page's bridge messages through CEF's message router, with `handler`
// (owned). The page stays on internal URLs and cannot open popups.
class InternalPageClient : public CefClient,
                           public CefContextMenuHandler,
                           public CefDisplayHandler,
                           public CefKeyboardHandler,
                           public CefLifeSpanHandler,
                           public CefRequestHandler {
 public:
  InternalPageClient(
      InternalPageDelegate* delegate,
      std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler);
  ~InternalPageClient() override;
  InternalPageClient(const InternalPageClient&) = delete;
  InternalPageClient& operator=(const InternalPageClient&) = delete;

  // UI thread. The platform window is going away: no more calls to it.
  void DetachDelegate();

  // CefClient:
  CefRefPtr<CefContextMenuHandler> GetContextMenuHandler() override {
    return this;
  }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
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
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;

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
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefRequestHandler:
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;
  void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                 TerminationStatus status,
                                 int error_code,
                                 const CefString& error_string) override;

 private:
  InternalPageDelegate* delegate_;  // Not owned. Null after DetachDelegate().
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler_;
  // "?" buttons of every internal page (016).
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> help_handler_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;

  IMPLEMENT_REFCOUNTING(InternalPageClient);
};

}  // namespace brouk

#endif  // BROUK_CEF_INTERNAL_PAGE_CLIENT_H_
