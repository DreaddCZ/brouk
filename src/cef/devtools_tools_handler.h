#ifndef BROUK_CEF_DEVTOOLS_TOOLS_HANDLER_H_
#define BROUK_CEF_DEVTOOLS_TOOLS_HANDLER_H_

#include <utility>

#include "cef/devtools_channel.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// Answers the tools page (brouk://tools, 006) shown in one tab's dock:
//   {"type":"tools.page"}                      -> {"url": "<tab URL>"}
//   {"type":"tools.protocol","method":"...","params":{...}} -> result
// Only that page may ask, and only protocol methods allowed by
// IsAllowedToolsProtocolMethod; everything goes to the dock's own tab.
// UI thread.
class DevToolsToolsHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit DevToolsToolsHandler(CefRefPtr<DevToolsChannel> channel)
      : channel_(std::move(channel)) {}
  DevToolsToolsHandler(const DevToolsToolsHandler&) = delete;
  DevToolsToolsHandler& operator=(const DevToolsToolsHandler&) = delete;

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;

 private:
  CefRefPtr<DevToolsChannel> channel_;
};

}  // namespace brouk

#endif  // BROUK_CEF_DEVTOOLS_TOOLS_HANDLER_H_
