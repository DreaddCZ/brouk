#ifndef BROUK_CEF_DEVTOOLS_CHANNEL_H_
#define BROUK_CEF_DEVTOOLS_CHANNEL_H_

#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "core/request_log.h"
#include "include/cef_browser.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_registration.h"
#include "include/cef_values.h"

namespace brouk {

// Chrome DevTools Protocol commands of our own tools to one tab (006). The
// tab's protocol session is shared with the DevTools frontend; our commands
// use their own ids, and their answers go only to the caller. UI thread.
class DevToolsChannel : public CefDevToolsMessageObserver {
 public:
  // Gets the command's "result" object, or nullopt and the error message.
  using Done = std::function<void(CefRefPtr<CefDictionaryValue> result,
                                  const std::string& error)>;

  explicit DevToolsChannel(CefRefPtr<CefBrowser> tab);
  DevToolsChannel(const DevToolsChannel&) = delete;
  DevToolsChannel& operator=(const DevToolsChannel&) = delete;

  // The tab is going away: no more commands, pending ones are dropped.
  void Close();

  void Send(const std::string& method,
            const CefRefPtr<CefDictionaryValue>& params,
            Done done);
  // The tab's current main frame URL ("" when closed).
  std::string PageUrl() const;

  // Records the tab's requests from now on (006: while dev tools are on).
  void StartRecording();
  const RequestLog& requests() const { return requests_; }
  void ClearRequests() { requests_.Clear(); }

  // CefDevToolsMessageObserver:
  bool OnDevToolsMessage(CefRefPtr<CefBrowser> browser,
                         const void* message,
                         size_t message_size) override;

 private:
  CefRefPtr<CefBrowser> tab_;
  CefRefPtr<CefRegistration> registration_;
  // Above the frontend's ids and those of DevToolsFrontendClient.
  int next_id_ = 1'500'000'000;
  std::map<int, Done> pending_;
  RequestLog requests_;
  bool recording_ = false;

  void HandleNetworkEvent(std::string_view message);

  IMPLEMENT_REFCOUNTING(DevToolsChannel);
};

}  // namespace brouk

#endif  // BROUK_CEF_DEVTOOLS_CHANNEL_H_
