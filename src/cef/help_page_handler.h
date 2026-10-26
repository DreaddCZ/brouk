#ifndef BROUK_CEF_HELP_PAGE_HANDLER_H_
#define BROUK_CEF_HELP_PAGE_HANDLER_H_

#include <functional>

#include "core/settings.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// Answers the help page's bridge messages (web/help/api.ts, 016): the parts
// of help made from the code, so they never go out of date. "help.state"
// gives every shortcut action with its shortcuts in effect now, and every
// setting of the settings page with its key, default and scope. Only
// brouk://help may ask. UI thread.
class HelpPageHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  // `global_settings` gives the current global settings (shortcuts).
  explicit HelpPageHandler(std::function<const Settings&()> global_settings)
      : global_settings_(std::move(global_settings)) {}
  HelpPageHandler(const HelpPageHandler&) = delete;
  HelpPageHandler& operator=(const HelpPageHandler&) = delete;

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;

 private:
  std::function<const Settings&()> global_settings_;
};

}  // namespace brouk

#endif  // BROUK_CEF_HELP_PAGE_HANDLER_H_
