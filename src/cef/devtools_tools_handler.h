#ifndef BROUK_CEF_DEVTOOLS_TOOLS_HANDLER_H_
#define BROUK_CEF_DEVTOOLS_TOOLS_HANDLER_H_

#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "cef/devtools_channel.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// What the tools need from the platform (006). UI thread.
class ToolsHost {
 public:
  struct File {
    std::string name;  // Suggested file name.
    std::string content;
  };
  // saved is false when the user cancelled. `extra` is saved next to the
  // main file, under its own name.
  using Done = std::function<void(bool saved, std::string error)>;
  virtual ~ToolsHost() = default;
  // Saves exported files (Postman collection and environment); the user
  // chooses where.
  virtual void SaveFiles(File main, std::optional<File> extra, Done done) = 0;
  // Header names whose values are secrets in exports (a setting).
  virtual std::string SecretHeaderNames() = 0;
};

// Answers the tools page (brouk://tools, 006) shown in one tab's dock:
//   {"type":"tools.page"}       -> {"url": "<tab URL>", "secretHeaders": ".."}
//   {"type":"tools.protocol","method":"...","params":{...}} -> result
//   {"type":"tools.requests","version":n}      -> recorded requests (list)
//   {"type":"tools.request","seq":n}           -> one request, with headers
//   {"type":"tools.clearRequests"}
//   {"type":"tools.tokens"}                    -> JWTs in recorded headers
//   {"type":"tools.save","name":..,"content":..,"extra":{name,content}?}
// Only that page may ask, and only protocol methods allowed by
// IsAllowedToolsProtocolMethod; everything goes to the dock's own tab.
// UI thread.
class DevToolsToolsHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  DevToolsToolsHandler(CefRefPtr<DevToolsChannel> channel, ToolsHost* host)
      : channel_(std::move(channel)), host_(host) {}
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
  ToolsHost* host_;  // Not owned; outlives the tools page.
};

}  // namespace brouk

#endif  // BROUK_CEF_DEVTOOLS_TOOLS_HANDLER_H_
