#include "cef/tab_client.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_image.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// Favicons are drawn at 16 pt; 32 px covers Retina displays.
constexpr std::uint32_t kFaviconMaxSize = 32;

class FaviconCallback : public CefDownloadImageCallback {
 public:
  using Done = void (*)(const CefRefPtr<TabClient>&,
                        const std::string&,
                        const std::vector<std::uint8_t>&);

  FaviconCallback(CefRefPtr<TabClient> client, Done done)
      : client_(std::move(client)), done_(done) {}
  FaviconCallback(const FaviconCallback&) = delete;
  FaviconCallback& operator=(const FaviconCallback&) = delete;

  // UI thread.
  void OnDownloadImageFinished(const CefString& image_url,
                               int /*http_status_code*/,
                               CefRefPtr<CefImage> image) override {
    CEF_REQUIRE_UI_THREAD();
    std::vector<std::uint8_t> png;
    if (image && !image->IsEmpty()) {
      int width = 0;
      int height = 0;
      CefRefPtr<CefBinaryValue> data =
          image->GetAsPNG(2.0f, /*with_transparency=*/true, width, height);
      if (data && data->GetSize() > 0) {
        png.resize(data->GetSize());
        data->GetData(png.data(), png.size(), 0);
      }
    }
    done_(client_, image_url.ToString(), png);
  }

 private:
  CefRefPtr<TabClient> client_;
  Done done_;

  IMPLEMENT_REFCOUNTING(FaviconCallback);
};

}  // namespace

void TabClient::DetachDelegate() {
  CEF_REQUIRE_UI_THREAD();
  delegate_ = nullptr;
}

void TabClient::OnAddressChange(CefRefPtr<CefBrowser> /*browser*/,
                                CefRefPtr<CefFrame> frame,
                                const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_ && frame->IsMain()) {
    delegate_->OnAddressChanged(url.ToString());
  }
}

void TabClient::OnTitleChange(CefRefPtr<CefBrowser> /*browser*/,
                              const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnTitleChanged(title.ToString());
  }
}

void TabClient::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                   const std::vector<CefString>& icon_urls) {
  CEF_REQUIRE_UI_THREAD();
  if (icon_urls.empty()) {
    favicon_url_.clear();
    if (delegate_) {
      delegate_->OnFaviconChanged({});
    }
    return;
  }
  DownloadFavicon(browser, icon_urls.front().ToString());
}

void TabClient::DownloadFavicon(const CefRefPtr<CefBrowser>& browser,
                                const std::string& url) {
  if (url == favicon_url_) {
    return;
  }
  favicon_url_ = url;
  auto done = [](const CefRefPtr<TabClient>& client,
                 const std::string& image_url,
                 const std::vector<std::uint8_t>& png) {
    // Ignore results for an icon the page has replaced meanwhile.
    if (client->delegate_ && image_url == client->favicon_url_) {
      client->delegate_->OnFaviconChanged(png);
    }
  };
  browser->GetHost()->DownloadImage(url, /*is_favicon=*/true, kFaviconMaxSize,
                                    /*bypass_cache=*/false,
                                    new FaviconCallback(this, done));
}

bool TabClient::OnKeyEvent(CefRefPtr<CefBrowser> /*browser*/,
                           const CefKeyEvent& event,
                           CefEventHandle os_event) {
  CEF_REQUIRE_UI_THREAD();
  return delegate_ && delegate_->OnUnhandledKeyEvent(event, os_event);
}

bool TabClient::OnBeforePopup(CefRefPtr<CefBrowser> /*browser*/,
                              CefRefPtr<CefFrame> /*frame*/,
                              int popup_id,
                              const CefString& target_url,
                              const CefString& /*target_frame_name*/,
                              WindowOpenDisposition target_disposition,
                              bool user_gesture,
                              const CefPopupFeatures& popup_features,
                              CefWindowInfo& window_info,
                              CefRefPtr<CefClient>& client,
                              CefBrowserSettings& /*settings*/,
                              CefRefPtr<CefDictionaryValue>& /*extra_info*/,
                              bool* /*no_javascript_access*/) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_) {
    return true;  // Tab is closing: cancel.
  }
  PopupRequest request;
  request.popup_id = popup_id;
  request.target_url = target_url.ToString();
  request.disposition = target_disposition;
  request.user_gesture = user_gesture;
  request.wants_popup_window =
      target_disposition == CEF_WOD_NEW_POPUP || popup_features.isPopup != 0;
  std::optional<PopupHost> host = delegate_->OnBeforePopup(request);
  if (!host || !host->parent_view || !host->client) {
    return true;  // Blocked.
  }
  // Keep the popup in the opener's renderer so window.opener works (sign-in
  // popups rely on it); only where it is hosted changes.
  window_info.SetAsChild(host->parent_view, host->bounds);
  window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  client = host->client;
  return false;
}

void TabClient::OnBeforePopupAborted(CefRefPtr<CefBrowser> /*browser*/,
                                     int popup_id) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnPopupAborted(popup_id);
  }
}

void TabClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnBrowserCreated(browser);
  }
}

bool TabClient::DoClose(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  // The browser lives in a tab, not in its own window: never let CEF send a
  // close to the whole window. The platform removes the tab's view instead,
  // which completes the close.
  if (delegate_) {
    delegate_->OnCloseReady();
  }
  return true;
}

void TabClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnBrowserClosed();
  }
}

void TabClient::OnLoadingStateChange(CefRefPtr<CefBrowser> /*browser*/,
                                     bool is_loading,
                                     bool can_go_back,
                                     bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnLoadingStateChanged(is_loading, can_go_back, can_go_forward);
  }
}

}  // namespace brouk
