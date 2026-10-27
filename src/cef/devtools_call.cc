#include "cef/devtools_call.h"

#include <string>
#include <utility>

#include "cef/function_task.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_registration.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// Above the ids of the DevTools frontend, DevToolsChannel and the site data
// tool, so answers never mix.
int g_next_id = 1'800'000'000;

class CallObserver : public CefDevToolsMessageObserver {
 public:
  CallObserver(int id, DevToolsCallDone done)
      : id_(id), done_(std::move(done)) {}

  void SetRegistration(CefRefPtr<CefRegistration> registration) {
    registration_ = std::move(registration);
  }

  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> /*browser*/,
                              int message_id,
                              bool success,
                              const void* result,
                              size_t result_size) override {
    CEF_REQUIRE_UI_THREAD();
    if (message_id != id_) {
      return;
    }
    Finish(success,
           result ? std::string(static_cast<const char*>(result), result_size)
                  : std::string());
  }
  void OnDevToolsAgentDetached(CefRefPtr<CefBrowser> /*browser*/) override {
    Finish(false, {});  // The browser went away: nothing more will come.
  }

  void Finish(bool ok, std::string result) {
    if (!done_) {
      return;
    }
    DevToolsCallDone done = std::move(done_);
    done_ = nullptr;
    // Unregistering releases this observer; not from inside its callback.
    CefRefPtr<CefRegistration> registration = std::move(registration_);
    CefPostTask(TID_UI,
                new FunctionTask([done, registration, ok,
                                  result = std::move(result)]() mutable {
                  registration = nullptr;
                  done(ok, std::move(result));
                }));
  }

 private:
  int id_;
  DevToolsCallDone done_;
  CefRefPtr<CefRegistration> registration_;
  IMPLEMENT_REFCOUNTING(CallObserver);
};

}  // namespace

void CallDevToolsMethod(const CefRefPtr<CefBrowser>& browser,
                        const std::string& method,
                        const CefRefPtr<CefDictionaryValue>& params,
                        DevToolsCallDone done) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser) {
    CefPostTask(TID_UI, new FunctionTask([done] { done(false, {}); }));
    return;
  }
  int id = g_next_id++;
  CefRefPtr<CallObserver> observer = new CallObserver(id, std::move(done));
  observer->SetRegistration(
      browser->GetHost()->AddDevToolsMessageObserver(observer));
  if (browser->GetHost()->ExecuteDevToolsMethod(id, method, params) == 0) {
    observer->Finish(false, {});
  }
}

}  // namespace brouk
