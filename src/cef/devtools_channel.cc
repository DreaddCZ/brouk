#include "cef/devtools_channel.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "cef/json_util.h"
#include "cef/own_request_loader.h"
#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr int kFirstId = 1'500'000'000;

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

}  // namespace

DevToolsChannel::DevToolsChannel(CefRefPtr<CefBrowser> tab)
    : tab_(std::move(tab)) {}

void DevToolsChannel::Close() {
  CEF_REQUIRE_UI_THREAD();
  if (tab_ && fetch_enabled_) {
    // Never leave the tab intercepted: its requests would hang. Sent at
    // once, as nothing waits for the answer.
    CefRefPtr<CefDictionaryValue> message = CefDictionaryValue::Create();
    message->SetInt("id", next_id_++);
    message->SetString("method", "Fetch.disable");
    std::string json = ToJson(message);
    tab_->GetHost()->SendDevToolsMessage(json.data(), json.size());
  }
  fetch_enabled_ = false;
  rules_affected_ = nullptr;
  registration_ = nullptr;
  tab_ = nullptr;
  pending_.clear();
}

std::string DevToolsChannel::PageUrl() const {
  CEF_REQUIRE_UI_THREAD();
  return tab_ ? tab_->GetMainFrame()->GetURL().ToString() : std::string();
}

void DevToolsChannel::Send(const std::string& method,
                           const CefRefPtr<CefDictionaryValue>& params,
                           Done done) {
  CEF_REQUIRE_UI_THREAD();
  if (!tab_) {
    done(nullptr, "The tab has closed.");
    return;
  }
  if (!registration_) {
    registration_ = tab_->GetHost()->AddDevToolsMessageObserver(this);
  }
  int id = next_id_++;
  CefRefPtr<CefDictionaryValue> message = CefDictionaryValue::Create();
  message->SetInt("id", id);
  message->SetString("method", method);
  message->SetDictionary(
      "params", params ? params->Copy(false) : CefDictionaryValue::Create());
  pending_[id] = std::move(done);
  if (dispatching_) {
    CefPostTask(TID_UI, base::BindOnce(&DevToolsChannel::SendNow,
                                       CefRefPtr<DevToolsChannel>(this), id,
                                       ToJson(message)));
    return;
  }
  SendNow(id, ToJson(message));
}

void DevToolsChannel::SendNow(int id, const std::string& json) {
  CEF_REQUIRE_UI_THREAD();
  auto it = pending_.find(id);
  if (it == pending_.end()) {
    return;  // Closed meanwhile.
  }
  if (!tab_ ||
      !tab_->GetHost()->SendDevToolsMessage(json.data(), json.size())) {
    Done failed = std::move(it->second);
    pending_.erase(it);
    failed(nullptr, "The command could not be sent.");
  }
}

void DevToolsChannel::SetRules(RequestRuleSet rules,
                               std::function<void()> affected) {
  CEF_REQUIRE_UI_THREAD();
  rules_ = std::move(rules);
  rules_affected_ = std::move(affected);
  UpdateFetch();
}

void DevToolsChannel::UpdateFetch() {
  // Only the request stage: response header changes made at the response
  // stage are lost in CEF 154, so those requests are loaded by Brouk itself
  // (LoadForResponseRules).
  bool enable = HasActiveRules(rules_);
  if (enable == fetch_enabled_ || !tab_) {
    return;
  }
  fetch_enabled_ = enable;
  auto ignore = [](const CefRefPtr<CefDictionaryValue>&, const std::string&) {};
  if (!enable) {
    Send("Fetch.disable", nullptr, ignore);
    return;
  }
  CefRefPtr<CefListValue> patterns = CefListValue::Create();
  CefRefPtr<CefDictionaryValue> request = CefDictionaryValue::Create();
  request->SetString("urlPattern", "*");
  request->SetString("requestStage", "Request");
  patterns->SetDictionary(0, request);
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetList("patterns", patterns);
  Send("Fetch.enable", params, ignore);
}

namespace {

CefRefPtr<CefListValue> HeaderEntryList(const Headers& headers) {
  CefRefPtr<CefListValue> list = CefListValue::Create();
  std::size_t index = 0;
  for (const auto& [name, value] : headers) {
    CefRefPtr<CefDictionaryValue> entry = CefDictionaryValue::Create();
    entry->SetString("name", name);
    entry->SetString("value", value);
    list->SetDictionary(index++, entry);
  }
  return list;
}

// The request's headers from a Fetch.requestPaused "request" object.
Headers RequestHeadersOf(const CefRefPtr<CefDictionaryValue>& request) {
  Headers headers;
  if (!request || request->GetType("headers") != VTYPE_DICTIONARY) {
    return headers;
  }
  CefRefPtr<CefDictionaryValue> object = request->GetDictionary("headers");
  CefDictionaryValue::KeyList names;
  object->GetKeys(names);
  for (const CefString& name : names) {
    if (object->GetType(name) == VTYPE_STRING) {
      headers.emplace_back(name.ToString(), object->GetString(name).ToString());
    }
  }
  return headers;
}

// The request for Brouk to load itself, or nullopt when it cannot be:
// streams, or a body the event does not include.
std::optional<OwnRequestLoader::Request> OwnRequestOf(
    const CefRefPtr<CefDictionaryValue>& request,
    const std::string& url,
    const Headers& headers) {
  if (!request || request->GetType("method") != VTYPE_STRING ||
      !CanLoadForResponseRules(headers)) {
    return std::nullopt;
  }
  OwnRequestLoader::Request own;
  own.method = request->GetString("method").ToString();
  own.url = url;
  for (const auto& [name, value] : headers) {
    if (std::ranges::equal(
            name, std::string_view("referer"), [](char a, char b) {
              return std::tolower(static_cast<unsigned char>(a)) == b;
            })) {
      own.referrer = value;
    }
  }
  if (request->GetType("hasPostData") == VTYPE_BOOL &&
      request->GetBool("hasPostData")) {
    if (request->GetType("postDataEntries") != VTYPE_LIST) {
      return std::nullopt;
    }
    CefRefPtr<CefListValue> entries = request->GetList("postDataEntries");
    std::string body;
    for (std::size_t i = 0; i < entries->GetSize(); ++i) {
      CefRefPtr<CefDictionaryValue> entry =
          entries->GetType(i) == VTYPE_DICTIONARY ? entries->GetDictionary(i)
                                                  : nullptr;
      if (!entry || entry->GetType("bytes") != VTYPE_STRING) {
        return std::nullopt;  // E.g. a file upload.
      }
      CefRefPtr<CefBinaryValue> bytes =
          CefBase64Decode(entry->GetString("bytes"));
      if (!bytes) {
        return std::nullopt;
      }
      std::size_t size = bytes->GetSize();
      std::string part(size, '\0');
      bytes->GetData(part.data(), size, 0);
      body += part;
    }
    own.body = std::move(body);
  }
  return own;
}

}  // namespace

void DevToolsChannel::HandleRequestPaused(std::string_view message) {
  auto ignore = [](const CefRefPtr<CefDictionaryValue>&, const std::string&) {};
  CefRefPtr<CefValue> parsed = ParseJson(message);
  CefRefPtr<CefDictionaryValue> event =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  CefRefPtr<CefDictionaryValue> params =
      event && event->GetType("params") == VTYPE_DICTIONARY
          ? event->GetDictionary("params")
          : nullptr;
  if (!params || params->GetType("requestId") != VTYPE_STRING) {
    return;  // Nothing to continue.
  }
  std::string id = params->GetString("requestId").ToString();
  CefRefPtr<CefDictionaryValue> reply = CefDictionaryValue::Create();
  reply->SetString("requestId", id);
  CefRefPtr<CefDictionaryValue> request =
      params->GetType("request") == VTYPE_DICTIONARY
          ? params->GetDictionary("request")
          : nullptr;
  std::string url = request && request->GetType("url") == VTYPE_STRING
                        ? request->GetString("url").ToString()
                        : std::string();
  if (params->HasKey("responseStatusCode") ||
      params->HasKey("responseErrorReason")) {
    Send("Fetch.continueRequest", reply, ignore);  // Not paused by us.
    return;
  }
  RequestRuleDecision decision = DecideRequest(rules_, url);
  Headers headers = RequestHeadersOf(request);
  std::optional<OwnRequestLoader::Request> own;
  if (!decision.block && !decision.redirect &&
      decision.changes_response_headers) {
    own = OwnRequestOf(request, url, headers);
  }
  bool load_ourselves = own.has_value();
  if (decision.block) {
    reply->SetString("errorReason", "BlockedByClient");
    Send("Fetch.failRequest", reply, ignore);
  } else if (decision.redirect) {
    Headers location = {{"Location", *decision.redirect}};
    reply->SetInt("responseCode", 307);
    reply->SetList("responseHeaders", HeaderEntryList(location));
    Send("Fetch.fulfillRequest", reply, ignore);
  } else if (own) {
    own->headers =
        HeadersForOwnRequest(ApplyRequestHeaderRules(rules_, url, headers));
    LoadForResponseRules(id, *own);
  } else if (decision.changes_request_headers) {
    reply->SetList("headers", HeaderEntryList(ApplyRequestHeaderRules(
                                  rules_, url, std::move(headers))));
    Send("Fetch.continueRequest", reply, ignore);
  } else {
    Send("Fetch.continueRequest", reply, ignore);
  }
  if ((decision.block || decision.redirect ||
       decision.changes_request_headers || load_ourselves) &&
      rules_affected_) {
    rules_affected_();
  }
}

void DevToolsChannel::LoadForResponseRules(
    const std::string& id,
    const OwnRequestLoader::Request& request) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<DevToolsChannel> self(this);
  std::string url = request.url;
  OwnRequestLoader::Start(
      tab_->GetHost()->GetRequestContext(), request,
      [self, id, url](std::optional<OwnRequestLoader::Response> response) {
        self->AnswerLoaded(id, url, std::move(response));
      });
}

void DevToolsChannel::AnswerLoaded(
    const std::string& id,
    const std::string& url,
    std::optional<OwnRequestLoader::Response> response) {
  CEF_REQUIRE_UI_THREAD();
  if (!tab_) {
    return;  // Closed meanwhile; the tab dropped the request.
  }
  auto ignore = [](const CefRefPtr<CefDictionaryValue>&, const std::string&) {};
  CefRefPtr<CefDictionaryValue> reply = CefDictionaryValue::Create();
  reply->SetString("requestId", id);
  if (!response) {
    reply->SetString("errorReason", "Failed");
    Send("Fetch.failRequest", reply, ignore);
    return;
  }
  CefRefPtr<CefDictionaryValue> fail = reply->Copy(false);
  reply->SetInt("responseCode", response->status);
  if (!response->status_text.empty()) {
    reply->SetString("responsePhrase", response->status_text);
  }
  reply->SetList(
      "responseHeaders",
      HeaderEntryList(ApplyResponseHeaderRules(
          rules_, url, HeadersForLoadedResponse(response->headers))));
  if (!response->body.empty()) {
    reply->SetString(
        "body", CefBase64Encode(response->body.data(), response->body.size()));
  }
  CefRefPtr<DevToolsChannel> self(this);
  Send("Fetch.fulfillRequest", reply,
       [self, fail](const CefRefPtr<CefDictionaryValue>& result,
                    const std::string& error) {
         if (!result && !error.empty() && self->tab_) {
           fail->SetString("errorReason", "Failed");
           self->Send(
               "Fetch.failRequest", fail,
               [](const CefRefPtr<CefDictionaryValue>&, const std::string&) {});
         }
       });
}

void DevToolsChannel::StartRecording() {
  CEF_REQUIRE_UI_THREAD();
  if (recording_) {
    return;
  }
  recording_ = true;
  Send("Network.enable", nullptr,
       [](const CefRefPtr<CefDictionaryValue>&, const std::string&) {});
}

namespace {

Headers HeadersFrom(const CefRefPtr<CefDictionaryValue>& dict,
                    const char* key) {
  Headers headers;
  if (!dict || dict->GetType(key) != VTYPE_DICTIONARY) {
    return headers;
  }
  CefRefPtr<CefDictionaryValue> object = dict->GetDictionary(key);
  CefDictionaryValue::KeyList names;
  object->GetKeys(names);
  for (const CefString& name : names) {
    if (object->GetType(name) == VTYPE_STRING) {
      headers.emplace_back(name.ToString(), object->GetString(name).ToString());
    }
  }
  return headers;
}

std::string StringOf(const CefRefPtr<CefDictionaryValue>& dict,
                     const char* key) {
  return dict && dict->GetType(key) == VTYPE_STRING
             ? dict->GetString(key).ToString()
             : std::string();
}

double NumberOf(const CefRefPtr<CefDictionaryValue>& dict, const char* key) {
  if (!dict) {
    return 0;
  }
  if (dict->GetType(key) == VTYPE_DOUBLE) {
    return dict->GetDouble(key);
  }
  if (dict->GetType(key) == VTYPE_INT) {
    return dict->GetInt(key);
  }
  return 0;
}

CefRefPtr<CefDictionaryValue> DictOf(const CefRefPtr<CefDictionaryValue>& dict,
                                     const char* key) {
  return dict && dict->GetType(key) == VTYPE_DICTIONARY
             ? dict->GetDictionary(key)
             : nullptr;
}

}  // namespace

void DevToolsChannel::HandleNetworkEvent(std::string_view message) {
  CefRefPtr<CefValue> parsed = ParseJson(message);
  CefRefPtr<CefDictionaryValue> event =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  std::string method = StringOf(event, "method");
  CefRefPtr<CefDictionaryValue> params = DictOf(event, "params");
  std::string id = StringOf(params, "requestId");
  if (id.empty()) {
    return;
  }
  if (method == "Network.requestWillBeSent") {
    CefRefPtr<CefDictionaryValue> request = DictOf(params, "request");
    std::optional<std::string> post_data;
    if (request && request->GetType("postData") == VTYPE_STRING) {
      post_data = request->GetString("postData").ToString();
    }
    requests_.OnRequest(id, StringOf(request, "url"),
                        StringOf(request, "method"), StringOf(params, "type"),
                        HeadersFrom(request, "headers"), std::move(post_data),
                        NumberOf(params, "wallTime"),
                        NumberOf(params, "timestamp"));
  } else if (method == "Network.requestWillBeSentExtraInfo") {
    requests_.OnRequestExtraHeaders(id, HeadersFrom(params, "headers"));
  } else if (method == "Network.responseReceived") {
    CefRefPtr<CefDictionaryValue> response = DictOf(params, "response");
    requests_.OnResponse(id, static_cast<int>(NumberOf(response, "status")),
                         StringOf(response, "mimeType"),
                         HeadersFrom(response, "headers"));
  } else if (method == "Network.responseReceivedExtraInfo") {
    requests_.OnResponseExtraHeaders(id, HeadersFrom(params, "headers"));
  } else if (method == "Network.loadingFinished") {
    requests_.OnFinished(
        id, NumberOf(params, "timestamp"),
        static_cast<std::int64_t>(NumberOf(params, "encodedDataLength")));
  } else if (method == "Network.loadingFailed") {
    requests_.OnFailed(id, NumberOf(params, "timestamp"),
                       StringOf(params, "errorText"));
  }
}

bool DevToolsChannel::OnDevToolsMessage(CefRefPtr<CefBrowser> /*browser*/,
                                        const void* message,
                                        size_t message_size) {
  CEF_REQUIRE_UI_THREAD();
  dispatching_ = true;
  bool handled = Dispatch(
      std::string_view(static_cast<const char*>(message), message_size));
  dispatching_ = false;
  return handled;
}

bool DevToolsChannel::Dispatch(std::string_view text) {
  constexpr std::string_view kRequestPaused =
      R"({"method":"Fetch.requestPaused")";
  if (text.starts_with(kRequestPaused)) {
    HandleRequestPaused(text);
    return true;
  }
  constexpr std::string_view kNetworkEvent = R"({"method":"Network.)";
  if (recording_ && text.starts_with(kNetworkEvent)) {
    HandleNetworkEvent(text);
    return false;  // The DevTools frontend wants it too.
  }
  // Answers to our commands: {"id":<ours>,...}. Everything else is for the
  // DevTools frontend.
  constexpr std::string_view kPrefix = R"({"id":)";
  if (!text.starts_with(kPrefix)) {
    return false;
  }
  long long id = 0;
  std::string_view number = text.substr(kPrefix.size());
  if (std::from_chars(number.data(), number.data() + number.size(), id).ec !=
          std::errc() ||
      id < kFirstId) {
    return false;
  }
  auto it = pending_.find(static_cast<int>(id));
  if (it == pending_.end()) {
    return true;
  }
  Done done = std::move(it->second);
  pending_.erase(it);
  CefRefPtr<CefValue> parsed = ParseJson(text);
  CefRefPtr<CefDictionaryValue> answer =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  if (answer && answer->GetType("result") == VTYPE_DICTIONARY) {
    done(answer->GetDictionary("result"), {});
    return true;
  }
  std::string error = "The command failed.";
  if (answer && answer->GetType("error") == VTYPE_DICTIONARY) {
    CefRefPtr<CefDictionaryValue> details = answer->GetDictionary("error");
    if (details->GetType("message") == VTYPE_STRING) {
      error = details->GetString("message").ToString();
    }
  }
  done(nullptr, error);
  return true;
}

}  // namespace brouk
