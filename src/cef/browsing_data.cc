#include "cef/browsing_data.h"

#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_cookie.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_registration.h"
#include "include/cef_task.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// Message ids of our DevTools calls; far from those of the developer tools
// (006), which share the browser.
constexpr int kFirstMessageId = 900000;
// How long to wait for the cookie store.
constexpr std::int64_t kCookieTimeoutMs = 10000;

class FunctionTask : public CefTask {
 public:
  explicit FunctionTask(std::function<void()> function)
      : function_(std::move(function)) {}
  void Execute() override { function_(); }

 private:
  std::function<void()> function_;
  IMPLEMENT_REFCOUNTING(FunctionTask);
};

std::int64_t SecondsOf(const cef_basetime_t& time) {
  cef_time_t exploded{};
  std::time_t seconds = 0;
  if (cef_time_from_basetime(time, &exploded) == 0 ||
      cef_time_to_timet(&exploded, &seconds) == 0) {
    return 0;
  }
  return static_cast<std::int64_t>(seconds);
}

std::string HostOfCookie(const CefCookie& cookie) {
  std::string domain = CefString(&cookie.domain).ToString();
  if (!domain.empty() && domain.front() == '.') {
    domain.erase(0, 1);
  }
  return domain;
}

// Counts (and deletes) cookies; reports when CEF releases it, which also
// happens when there are no cookies at all (Visit is then never called).
class CookieVisitor : public CefCookieVisitor {
 public:
  CookieVisitor(std::int64_t since, bool remove, CookiesDone done)
      : since_(since), remove_(remove), done_(std::move(done)) {}
  ~CookieVisitor() override {
    CookiesDone done = std::move(done_);
    int count = count_;
    std::set<std::string> hosts = std::move(hosts_);
    CefPostTask(TID_UI,
                new FunctionTask([done, count, hosts] { done(count, hosts); }));
  }

  bool Visit(const CefCookie& cookie,
             int /*count*/,
             int /*total*/,
             bool& delete_cookie) override {
    if (since_ == 0 || SecondsOf(cookie.creation) >= since_) {
      ++count_;
      hosts_.insert(HostOfCookie(cookie));
      delete_cookie = remove_;
    }
    return true;
  }

 private:
  std::int64_t since_;
  bool remove_;
  CookiesDone done_;
  int count_ = 0;
  std::set<std::string> hosts_;
  IMPLEMENT_REFCOUNTING(CookieVisitor);
};

// Waits for the results of our Storage.clearDataForOrigin calls.
class SiteDataObserver : public CefDevToolsMessageObserver {
 public:
  SiteDataObserver(std::set<int> pending, std::function<void()> done)
      : pending_(std::move(pending)), done_(std::move(done)) {}

  void SetRegistration(CefRefPtr<CefRegistration> registration) {
    registration_ = std::move(registration);
  }

  void OnDevToolsMethodResult(CefRefPtr<CefBrowser> /*browser*/,
                              int message_id,
                              bool /*success*/,
                              const void* /*result*/,
                              size_t /*result_size*/) override {
    CEF_REQUIRE_UI_THREAD();
    if (pending_.erase(message_id) == 0 || !pending_.empty()) {
      return;
    }
    Finish();
  }
  void OnDevToolsAgentDetached(CefRefPtr<CefBrowser> /*browser*/) override {
    Finish();  // The browser went away: nothing more will come.
  }

  void Finish() {
    if (!done_) {
      return;
    }
    std::function<void()> done = std::move(done_);
    done_ = nullptr;
    // Unregistering releases this observer; not from inside its callback.
    CefRefPtr<CefRegistration> registration = std::move(registration_);
    CefPostTask(TID_UI, new FunctionTask([done, registration]() mutable {
                  registration = nullptr;
                  done();
                }));
  }

 private:
  std::set<int> pending_;
  std::function<void()> done_;
  CefRefPtr<CefRegistration> registration_;
  IMPLEMENT_REFCOUNTING(SiteDataObserver);
};

class HelperClient : public CefClient, public CefLifeSpanHandler {
 public:
  HelperClient(std::function<void(CefRefPtr<CefBrowser>)> created,
               std::function<void()> closed)
      : created_(std::move(created)), closed_(std::move(closed)) {}

  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
    CEF_REQUIRE_UI_THREAD();
    if (created_) {
      std::function<void(CefRefPtr<CefBrowser>)> created = std::move(created_);
      created_ = nullptr;
      created(browser);
    }
  }
  void OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) override {
    CEF_REQUIRE_UI_THREAD();
    if (closed_) {
      std::function<void()> closed = std::move(closed_);
      closed_ = nullptr;
      closed();
    }
  }
  // No popups from the helper.
  bool OnBeforePopup(CefRefPtr<CefBrowser> /*browser*/,
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
                     bool* /*no_javascript_access*/) override {
    return true;
  }

 private:
  std::function<void(CefRefPtr<CefBrowser>)> created_;
  std::function<void()> closed_;
  IMPLEMENT_REFCOUNTING(HelperClient);
};

}  // namespace

void VisitCookiesSince(const CefRefPtr<CefRequestContext>& context,
                       std::int64_t since,
                       bool remove,
                       CookiesDone done) {
  CEF_REQUIRE_UI_THREAD();
  if (!context) {
    done(0, {});
    return;
  }
  // The cookie store of a profile without an open page is loaded first;
  // visiting before that would never finish.
  class Ready : public CefCompletionCallback {
   public:
    Ready(CefRefPtr<CefRequestContext> context,
          std::int64_t since,
          bool remove,
          CookiesDone done)
        : context_(std::move(context)),
          since_(since),
          remove_(remove),
          done_(std::move(done)) {}
    void OnComplete() override {
      CefRefPtr<CefCookieManager> manager = context_->GetCookieManager(nullptr);
      if (!manager) {
        done_(0, {});
        return;
      }
      // If visiting fails, the visitor is released at once and still
      // reports.
      manager->VisitAllCookies(
          new CookieVisitor(since_, remove_, std::move(done_)));
    }

   private:
    CefRefPtr<CefRequestContext> context_;
    std::int64_t since_;
    bool remove_;
    CookiesDone done_;
    IMPLEMENT_REFCOUNTING(Ready);
  };
  // Reported once: by the visitor, or with count -1 if the cookie store does
  // not answer in time (the dialog then says the count is unknown).
  auto once = std::make_shared<CookiesDone>(std::move(done));
  CookiesDone report = [once](int count, std::set<std::string> hosts) {
    if (*once) {
      CookiesDone call = std::move(*once);
      *once = nullptr;
      call(count, std::move(hosts));
    }
  };
  CefPostDelayedTask(TID_UI, new FunctionTask([report] { report(-1, {}); }),
                     kCookieTimeoutMs);
  context->GetCookieManager(new Ready(context, since, remove, report));
}

void ClearSiteData(const CefRefPtr<CefBrowser>& browser,
                   const std::vector<std::string>& origins,
                   std::function<void()> done) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser || origins.empty()) {
    done();
    return;
  }
  static int next_id = kFirstMessageId;
  std::set<int> ids;
  std::vector<std::pair<int, std::string>> calls;
  for (const std::string& origin : origins) {
    int id = next_id++;
    ids.insert(id);
    calls.emplace_back(id, origin);
  }
  CefRefPtr<SiteDataObserver> observer =
      new SiteDataObserver(ids, std::move(done));
  observer->SetRegistration(
      browser->GetHost()->AddDevToolsMessageObserver(observer));
  for (const auto& [id, origin] : calls) {
    CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
    params->SetString("origin", origin);
    // Cookies are cleared separately, by time.
    params->SetString("storageTypes",
                      "file_systems,indexeddb,local_storage,shader_cache,"
                      "websql,service_workers,cache_storage,interest_groups,"
                      "shared_storage,storage_buckets");
    if (browser->GetHost()->ExecuteDevToolsMethod(
            id, "Storage.clearDataForOrigin", params) == 0) {
      observer->OnDevToolsMethodResult(browser, id, false, nullptr, 0);
    }
  }
}

std::vector<std::string> OriginsOfHosts(const std::set<std::string>& hosts) {
  std::vector<std::string> origins;
  for (const std::string& host : hosts) {
    if (host.empty()) {
      continue;  // Local files have no site data of their own.
    }
    origins.push_back("https://" + host);
    origins.push_back("http://" + host);
  }
  return origins;
}

void ClearHttpCache(const CefRefPtr<CefRequestContext>& context,
                    std::function<void()> done) {
  CEF_REQUIRE_UI_THREAD();
  if (!context) {
    done();
    return;
  }
  class Callback : public CefCompletionCallback {
   public:
    explicit Callback(std::function<void()> done) : done_(std::move(done)) {}
    void OnComplete() override { done_(); }

   private:
    std::function<void()> done_;
    IMPLEMENT_REFCOUNTING(Callback);
  };
  context->ClearHttpCache(new Callback(std::move(done)));
}

CefRefPtr<CefClient> MakeHelperBrowserClient(
    std::function<void(CefRefPtr<CefBrowser>)> created,
    std::function<void()> closed) {
  return new HelperClient(std::move(created), std::move(closed));
}

}  // namespace brouk
