#ifndef BROUK_CEF_DEVTOOLS_CHANNEL_H_
#define BROUK_CEF_DEVTOOLS_CHANNEL_H_

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "cef/own_request_loader.h"
#include "core/request_log.h"
#include "core/request_rules.h"
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

  // The profile's request rules (006), applied to this tab's requests
  // through the protocol's Fetch domain while any rule is active.
  // `affected` is called when a rule changed a request.
  void SetRules(RequestRuleSet rules, std::function<void()> affected);

  // CefDevToolsMessageObserver:
  bool OnDevToolsMessage(CefRefPtr<CefBrowser> browser,
                         const void* message,
                         size_t message_size) override;

 private:
  void SendNow(int id, const std::string& json);
  // Response header rules (006): Brouk loads the request itself and answers
  // the paused request with the changed headers.
  void LoadForResponseRules(const std::string& id,
                            const OwnRequestLoader::Request& request);
  void AnswerLoaded(const std::string& id,
                    const std::string& url,
                    std::optional<OwnRequestLoader::Response> response);
  // Handles one message of OnDevToolsMessage; true if it was only for us.
  bool Dispatch(std::string_view text);

  CefRefPtr<CefBrowser> tab_;
  CefRefPtr<CefRegistration> registration_;
  // Above the frontend's ids and those of DevToolsFrontendClient.
  int next_id_ = 1'500'000'000;
  std::map<int, Done> pending_;
  // True while OnDevToolsMessage runs: commands sent then are posted, as
  // sending from inside CEF's observer callback re-enters its observer list.
  bool dispatching_ = false;
  RequestLog requests_;
  bool recording_ = false;
  RequestRuleSet rules_;
  std::function<void()> rules_affected_;
  // Whether Fetch.enable is on (requests are paused for the rules).
  bool fetch_enabled_ = false;

  void UpdateFetch();
  void HandleRequestPaused(std::string_view message);

  void HandleNetworkEvent(std::string_view message);

  IMPLEMENT_REFCOUNTING(DevToolsChannel);
};

}  // namespace brouk

#endif  // BROUK_CEF_DEVTOOLS_CHANNEL_H_
