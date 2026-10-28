#ifndef BROUK_CEF_STATIC_PAGE_HANDLER_H_
#define BROUK_CEF_STATIC_PAGE_HANDLER_H_

#include "include/wrapper/cef_message_router.h"

namespace brouk {

// The bridge of an internal page that needs nothing from the browser, such
// as the open-source licences (023): it answers no messages. ("help.open"
// is answered before any page handler.) UI thread.
class StaticPageHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  StaticPageHandler() = default;
  StaticPageHandler(const StaticPageHandler&) = delete;
  StaticPageHandler& operator=(const StaticPageHandler&) = delete;

  bool OnQuery(CefRefPtr<CefBrowser> /*browser*/,
               CefRefPtr<CefFrame> /*frame*/,
               int64_t /*query_id*/,
               const CefString& /*request*/,
               bool /*persistent*/,
               CefRefPtr<Callback> /*callback*/) override {
    return false;
  }
};

}  // namespace brouk

#endif  // BROUK_CEF_STATIC_PAGE_HANDLER_H_
