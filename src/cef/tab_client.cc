#include "cef/tab_client.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/crash_error_code.h"
#include "include/cef_image.h"
#include "include/cef_parser.h"
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

bool TabClient::OnJSDialog(CefRefPtr<CefBrowser> /*browser*/,
                           const CefString& origin_url,
                           JSDialogType dialog_type,
                           const CefString& message_text,
                           const CefString& default_prompt_text,
                           CefRefPtr<CefJSDialogCallback> callback,
                           bool& suppress_message) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_ || dialog_guard_.IsSuppressed() || pending_dialog_) {
    // Suppressing (rather than answering) lets Chromium detect dialog spam.
    suppress_message = true;
    return false;
  }
  JsDialogRequest request;
  switch (dialog_type) {
    case JSDIALOGTYPE_CONFIRM:
      request.kind = JsDialogRequest::Kind::kConfirm;
      break;
    case JSDIALOGTYPE_PROMPT:
      request.kind = JsDialogRequest::Kind::kPrompt;
      break;
    default:
      request.kind = JsDialogRequest::Kind::kAlert;
      break;
  }
  // Chrome names the origin in the title, except for local files, which
  // have no meaningful origin ("This page says").
  std::string origin = origin_url.ToString();
  if (!origin.empty() && origin.rfind("file:", 0) != 0) {
    request.origin = CefFormatUrlForSecurityDisplay(origin_url).ToString();
  }
  request.message = message_text.ToString();
  request.default_prompt_text = default_prompt_text.ToString();
  request.offer_suppression = dialog_guard_.ShouldOfferSuppression();
  ShowDialog(request, callback);
  return true;
}

bool TabClient::OnBeforeUnloadDialog(CefRefPtr<CefBrowser> /*browser*/,
                                     const CefString& /*message_text*/,
                                     bool is_reload,
                                     CefRefPtr<CefJSDialogCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_ || pending_dialog_) {
    return false;
  }
  // Like Chrome, the page's own text is not shown (it could be misleading).
  JsDialogRequest request;
  request.kind = JsDialogRequest::Kind::kBeforeUnload;
  request.is_reload = is_reload;
  ShowDialog(request, callback);
  return true;
}

void TabClient::ShowDialog(const JsDialogRequest& request,
                           CefRefPtr<CefJSDialogCallback> callback) {
  pending_dialog_ = callback;
  CefRefPtr<TabClient> self(this);
  bool counts_for_spam = request.kind != JsDialogRequest::Kind::kBeforeUnload;
  delegate_->ShowJsDialog(
      request, [self, callback, counts_for_spam](
                   bool ok, const std::string& input, bool suppress_more) {
        CEF_REQUIRE_UI_THREAD();
        // Ignore answers to a dialog that was already reset by navigation.
        if (self->pending_dialog_ != callback) {
          return;
        }
        self->pending_dialog_ = nullptr;
        if (counts_for_spam) {
          self->dialog_guard_.OnDialogClosed(suppress_more);
        }
        callback->Continue(ok, input);
      });
}

void TabClient::OnResetDialogState(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  dialog_guard_.Reset();
  if (pending_dialog_) {
    pending_dialog_ = nullptr;
    if (delegate_) {
      delegate_->DismissJsDialog();
    }
  }
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

void TabClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> /*browser*/,
                                          TerminationStatus status,
                                          int error_code,
                                          const CefString& /*error_string*/) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_) {
    return;
  }
  RendererGoneReason reason = RendererGoneReason::kOther;
  switch (status) {
    case TS_PROCESS_CRASHED:
      reason = RendererGoneReason::kCrashed;
      break;
    case TS_PROCESS_WAS_KILLED:
      reason = RendererGoneReason::kKilled;
      break;
    case TS_PROCESS_OOM:
      reason = RendererGoneReason::kOutOfMemory;
      break;
    default:
      break;
  }
  delegate_->OnRendererGone(reason, CrashErrorCodeText(error_code));
}

bool TabClient::OnRenderProcessUnresponsive(
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefUnresponsiveProcessCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_) {
    return false;
  }
  delegate_->OnRendererUnresponsive([callback](bool exit_page) {
    if (exit_page) {
      callback->Terminate();
    } else {
      callback->Wait();
    }
  });
  return true;
}

void TabClient::OnRenderProcessResponsive(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnRendererResponsive();
  }
}

}  // namespace brouk
