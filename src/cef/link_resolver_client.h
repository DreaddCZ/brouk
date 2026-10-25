#ifndef BROUK_CEF_LINK_RESOLVER_CLIENT_H_
#define BROUK_CEF_LINK_RESOLVER_CLIENT_H_

#include <string>
#include <vector>

#include "core/link_routing_rules.h"
#include "include/cef_client.h"

namespace brouk {

// Platform side of resolving one opaque link wrapper (010). UI thread.
class LinkResolverDelegate {
 public:
  virtual ~LinkResolverDelegate() = default;
  virtual void OnBrowserCreated(CefRefPtr<CefBrowser> browser) = 0;
  // The link left the wrapper hosts for `url` (an http(s) URL). That
  // navigation was stopped: Brouk routes `url` instead. Called once.
  virtual void OnResolved(const std::string& url) = 0;
  // Remove the browser's view to complete closing (see DoClose).
  virtual void OnCloseReady() = 0;
  virtual void OnBrowserClosed() = 0;
  virtual void OnTitleChanged(const std::string& title) = 0;
};

// CefClient for the neutral browser that follows an opaque wrapper
// (Mimecast, …) in a throw-away request context. Main-frame navigations
// may stay on wrapper hosts (and pages that are not http, like error
// pages); the first one that leaves them, a popup or a download is stopped
// and reported as the real URL.
class LinkResolverClient : public CefClient,
                           public CefDisplayHandler,
                           public CefDownloadHandler,
                           public CefLifeSpanHandler,
                           public CefRequestHandler {
 public:
  LinkResolverClient(LinkResolverDelegate* delegate,
                     std::vector<LinkWrapperRule> user_wrappers);
  LinkResolverClient(const LinkResolverClient&) = delete;
  LinkResolverClient& operator=(const LinkResolverClient&) = delete;

  // UI thread. The platform side is going away: no more calls to it.
  void DetachDelegate();

  // CefClient:
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

  // CefDisplayHandler:
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;

  // CefDownloadHandler:
  bool CanDownload(CefRefPtr<CefBrowser> browser,
                   const CefString& url,
                   const CefString& request_method) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;

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

 private:
  // Reports `url` once, if it is a routable URL.
  void Resolve(const std::string& url);

  LinkResolverDelegate* delegate_;
  std::vector<LinkWrapperRule> user_wrappers_;
  bool resolved_ = false;

  IMPLEMENT_REFCOUNTING(LinkResolverClient);
};

}  // namespace brouk

#endif  // BROUK_CEF_LINK_RESOLVER_CLIENT_H_
