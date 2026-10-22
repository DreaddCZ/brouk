#include "cef/internal_page_client.h"

#include <memory>
#include <string>
#include <utility>

#include "core/internal_pages.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

InternalPageClient::InternalPageClient(
    InternalPageDelegate* delegate,
    std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler)
    : delegate_(delegate),
      handler_(std::move(handler)),
      router_(CefMessageRouterBrowserSide::Create(CefMessageRouterConfig())) {
  router_->AddHandler(handler_.get(), /*first=*/false);
}

InternalPageClient::~InternalPageClient() {
  // The router must not keep a pointer to the handler we are deleting.
  router_->RemoveHandler(handler_.get());
}

void InternalPageClient::DetachDelegate() {
  CEF_REQUIRE_UI_THREAD();
  delegate_ = nullptr;
}

bool InternalPageClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  return router_->OnProcessMessageReceived(browser, frame, source_process,
                                           message);
}

void InternalPageClient::OnTitleChange(CefRefPtr<CefBrowser> /*browser*/,
                                       const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnTitleChanged(title.ToString());
  }
}

bool InternalPageClient::OnKeyEvent(CefRefPtr<CefBrowser> /*browser*/,
                                    const CefKeyEvent& event,
                                    CefEventHandle os_event) {
  CEF_REQUIRE_UI_THREAD();
  return delegate_ && delegate_->OnUnhandledKeyEvent(event, os_event);
}

bool InternalPageClient::OnBeforePopup(
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefFrame> /*frame*/,
    int /*popup_id*/,
    const CefString& /*target_url*/,
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
  return true;  // Internal pages open no popups.
}

void InternalPageClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnBrowserCreated(browser);
  }
}

bool InternalPageClient::DoClose(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  // As for tabs: the platform removes the browser's view, which completes
  // the close; CEF must not close the window by itself.
  if (delegate_) {
    delegate_->OnCloseReady();
  }
  return true;
}

void InternalPageClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  router_->OnBeforeClose(browser);
  if (delegate_) {
    delegate_->OnBrowserClosed();
  }
}

bool InternalPageClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        CefRefPtr<CefRequest> request,
                                        bool /*user_gesture*/,
                                        bool /*is_redirect*/) {
  CEF_REQUIRE_UI_THREAD();
  // An internal page window shows internal pages only.
  if (!IsInternalPageUrl(request->GetURL().ToString())) {
    return true;
  }
  router_->OnBeforeBrowse(browser, frame);
  return false;
}

void InternalPageClient::OnRenderProcessTerminated(
    CefRefPtr<CefBrowser> browser,
    TerminationStatus /*status*/,
    int /*error_code*/,
    const CefString& /*error_string*/) {
  CEF_REQUIRE_UI_THREAD();
  router_->OnRenderProcessTerminated(browser);
}

}  // namespace brouk
