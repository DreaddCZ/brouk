#include "cef/link_resolver_client.h"

#include <string>
#include <utility>
#include <vector>

#include "core/link_unwrap.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

LinkResolverClient::LinkResolverClient(
    LinkResolverDelegate* delegate,
    std::vector<LinkWrapperRule> user_wrappers)
    : delegate_(delegate), user_wrappers_(std::move(user_wrappers)) {}

void LinkResolverClient::DetachDelegate() {
  CEF_REQUIRE_UI_THREAD();
  delegate_ = nullptr;
}

void LinkResolverClient::Resolve(const std::string& url) {
  if (resolved_ || !IsRoutableUrl(url)) {
    return;
  }
  resolved_ = true;
  if (delegate_) {
    delegate_->OnResolved(url);
  }
}

void LinkResolverClient::OnTitleChange(CefRefPtr<CefBrowser> /*browser*/,
                                       const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnTitleChanged(title.ToString());
  }
}

bool LinkResolverClient::CanDownload(CefRefPtr<CefBrowser> /*browser*/,
                                     const CefString& url,
                                     const CefString& /*request_method*/) {
  CEF_REQUIRE_UI_THREAD();
  // Nothing is downloaded in the neutral context; the URL is routed and
  // downloads in the chosen profile.
  Resolve(url.ToString());
  return false;
}

bool LinkResolverClient::OnBeforeDownload(
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefDownloadItem> download_item,
    const CefString& /*suggested_name*/,
    CefRefPtr<CefBeforeDownloadCallback> /*callback*/) {
  CEF_REQUIRE_UI_THREAD();
  // Not reached while CanDownload refuses; cancelled in any case.
  Resolve(download_item->GetURL().ToString());
  return true;
}

bool LinkResolverClient::OnBeforePopup(
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefFrame> /*frame*/,
    int /*popup_id*/,
    const CefString& target_url,
    const CefString& /*target_frame_name*/,
    WindowOpenDisposition /*target_disposition*/,
    bool /*user_gesture*/,
    const CefPopupFeatures& /*popup_features*/,
    CefWindowInfo& /*window_info*/,
    CefRefPtr<CefClient>& /*client*/,
    CefBrowserSettings& /*settings*/,
    CefRefPtr<CefDictionaryValue>& /*extra_info*/,
    bool* /*no_javascript_access*/) {
  CEF_REQUIRE_UI_THREAD();
  // A wrapper page's "continue" may open the real page in a new window.
  std::string url = target_url.ToString();
  if (!LinkWrapperName(url, user_wrappers_)) {
    Resolve(url);
  }
  return true;
}

void LinkResolverClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnBrowserCreated(browser);
  }
}

bool LinkResolverClient::DoClose(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnCloseReady();
  }
  return true;
}

void LinkResolverClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnBrowserClosed();
  }
}

bool LinkResolverClient::OnBeforeBrowse(CefRefPtr<CefBrowser> /*browser*/,
                                        CefRefPtr<CefFrame> frame,
                                        CefRefPtr<CefRequest> request,
                                        bool /*user_gesture*/,
                                        bool /*is_redirect*/) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame->IsMain()) {
    return false;  // Frames inside a wrapper's own page.
  }
  if (resolved_) {
    return true;
  }
  std::string url = request->GetURL().ToString();
  if (LinkWrapperName(url, user_wrappers_) || !IsRoutableUrl(url)) {
    // Still on the wrapper (or one of its error or blank pages).
    return false;
  }
  Resolve(url);
  return true;
}

}  // namespace brouk
