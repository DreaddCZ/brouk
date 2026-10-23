#include "cef/devtools_tools_handler.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "cef/json_util.h"
#include "core/devtools_tools.h"
#include "core/internal_pages.h"
#include "core/request_log.h"
#include "core/request_rules.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxRequestBytes = std::size_t{1} * 1024 * 1024;
constexpr int kBadRequest = 400;
constexpr int kFailed = 500;

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

CefRefPtr<CefListValue> HeaderList(const Headers& headers) {
  CefRefPtr<CefListValue> list = CefListValue::Create();
  std::size_t index = 0;
  for (const auto& [name, value] : headers) {
    CefRefPtr<CefListValue> pair = CefListValue::Create();
    pair->SetString(0, name);
    pair->SetString(1, value);
    list->SetList(index++, pair);
  }
  return list;
}

CefRefPtr<CefDictionaryValue> Summary(const RecordedRequest& entry) {
  CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
  item->SetDouble("seq", static_cast<double>(entry.seq));
  item->SetString("id", entry.id);
  item->SetString("url", entry.url);
  item->SetString("method", entry.method);
  item->SetString("type", entry.type);
  item->SetInt("status", entry.status);
  item->SetString("mimeType", entry.mime_type);
  item->SetBool("finished", entry.finished);
  item->SetString("error", entry.error);
  item->SetDouble("started", entry.started);
  item->SetDouble("duration", entry.duration);
  item->SetDouble("size", static_cast<double>(entry.encoded_bytes));
  return item;
}

}  // namespace

bool DevToolsToolsHandler::OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                                   CefRefPtr<CefFrame> frame,
                                   int64_t /*query_id*/,
                                   const CefString& request,
                                   bool persistent,
                                   CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame || InternalPageOf(frame->GetURL().ToString()) != "tools") {
    return false;
  }
  std::string text = request.ToString();
  CefRefPtr<CefValue> parsed = text.size() <= kMaxRequestBytes && !persistent
                                   ? ParseJson(text)
                                   : nullptr;
  CefRefPtr<CefDictionaryValue> message =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  std::string type = message && message->GetType("type") == VTYPE_STRING
                         ? message->GetString("type").ToString()
                         : std::string();
  if (type == "tools.page") {
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetString("url", channel_->PageUrl());
    answer->SetString("secretHeaders",
                      host_ ? host_->SecretHeaderNames() : std::string());
    callback->Success(ToJson(answer));
    return true;
  }
  if (type == "tools.protocol") {
    std::string method = message->GetType("method") == VTYPE_STRING
                             ? message->GetString("method").ToString()
                             : std::string();
    CefRefPtr<CefDictionaryValue> params =
        message->GetType("params") == VTYPE_DICTIONARY
            ? message->GetDictionary("params")
            : nullptr;
    if (!IsAllowedToolsProtocolMethod(method)) {
      callback->Failure(kBadRequest, "This command is not allowed.");
      return true;
    }
    channel_->Send(method, params,
                   [callback](const CefRefPtr<CefDictionaryValue>& result,
                              const std::string& error) {
                     if (result) {
                       callback->Success(ToJson(result));
                     } else {
                       callback->Failure(kFailed, error);
                     }
                   });
    return true;
  }
  if (type == "tools.requests") {
    const RequestLog& log = channel_->requests();
    double known = message->GetType("version") == VTYPE_DOUBLE
                       ? message->GetDouble("version")
                   : message->GetType("version") == VTYPE_INT
                       ? message->GetInt("version")
                       : -1;
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetDouble("version", static_cast<double>(log.version()));
    if (known != static_cast<double>(log.version())) {
      CefRefPtr<CefListValue> list = CefListValue::Create();
      std::size_t index = 0;
      for (const RecordedRequest& entry : log.entries()) {
        list->SetDictionary(index++, Summary(entry));
      }
      answer->SetList("requests", list);
    }
    callback->Success(ToJson(answer));
    return true;
  }
  if (type == "tools.request") {
    double seq = message->GetType("seq") == VTYPE_INT ? message->GetInt("seq")
                 : message->GetType("seq") == VTYPE_DOUBLE
                     ? message->GetDouble("seq")
                     : 0;
    const RecordedRequest* entry =
        seq > 0 ? channel_->requests().Find(static_cast<std::uint64_t>(seq))
                : nullptr;
    if (!entry) {
      callback->Failure(kFailed, "This request is no longer recorded.");
      return true;
    }
    CefRefPtr<CefDictionaryValue> answer = Summary(*entry);
    answer->SetList("requestHeaders", HeaderList(entry->request_headers));
    answer->SetList("responseHeaders", HeaderList(entry->response_headers));
    if (entry->post_data) {
      answer->SetString("postData", *entry->post_data);
      answer->SetBool("postDataTruncated", entry->post_data_truncated);
    }
    callback->Success(ToJson(answer));
    return true;
  }
  if (type == "tools.clearRequests") {
    channel_->ClearRequests();
    callback->Success("");
    return true;
  }
  if (type == "tools.tokens") {
    CefRefPtr<CefListValue> list = CefListValue::Create();
    std::size_t index = 0;
    for (const FoundToken& token : FindTokens(channel_->requests(), 200)) {
      CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
      item->SetString("token", token.token);
      item->SetString("where", token.where);
      list->SetDictionary(index++, item);
    }
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetList("tokens", list);
    callback->Success(ToJson(answer));
    return true;
  }
  if (type == "tools.save") {
    auto file_from = [](const CefRefPtr<CefDictionaryValue>& dict)
        -> std::optional<ToolsHost::File> {
      if (!dict || dict->GetType("name") != VTYPE_STRING ||
          dict->GetType("content") != VTYPE_STRING) {
        return std::nullopt;
      }
      return ToolsHost::File{dict->GetString("name").ToString(),
                             dict->GetString("content").ToString()};
    };
    std::optional<ToolsHost::File> main = file_from(message);
    std::optional<ToolsHost::File> extra;
    if (message->GetType("extra") == VTYPE_DICTIONARY) {
      extra = file_from(message->GetDictionary("extra"));
    }
    if (!main || !host_) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    host_->SaveFiles(std::move(*main), std::move(extra),
                     [callback](bool saved, const std::string& error) {
                       if (!error.empty()) {
                         callback->Failure(kFailed, error);
                         return;
                       }
                       CefRefPtr<CefDictionaryValue> answer =
                           CefDictionaryValue::Create();
                       answer->SetBool("saved", saved);
                       callback->Success(ToJson(answer));
                     });
    return true;
  }
  if (type == "tools.rules" && host_) {
    ToolsHost::Rules rules = host_->RequestRules();
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetBool("paused", rules.set.paused);
    CefRefPtr<CefListValue> list = CefListValue::Create();
    std::size_t index = 0;
    for (const RequestRule& rule : rules.set.rules) {
      CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
      item->SetString("pattern", rule.pattern);
      item->SetString("action",
                      std::string(RequestRuleActionName(rule.action)));
      item->SetString("header", rule.header);
      item->SetString("value", rule.value);
      item->SetBool("enabled", rule.enabled);
      list->SetDictionary(index++, item);
    }
    answer->SetList("rules", list);
    CefRefPtr<CefListValue> problems = CefListValue::Create();
    index = 0;
    for (const std::string& problem : rules.problems) {
      problems->SetString(index++, problem);
    }
    answer->SetList("problems", problems);
    answer->SetString("profile", rules.profile_name);
    callback->Success(ToJson(answer));
    return true;
  }
  if (type == "tools.setRules" && host_) {
    RequestRuleSet set;
    set.paused =
        message->GetType("paused") == VTYPE_BOOL && message->GetBool("paused");
    CefRefPtr<CefListValue> list = message->GetType("rules") == VTYPE_LIST
                                       ? message->GetList("rules")
                                       : nullptr;
    if (!list || list->GetSize() > 500) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    for (std::size_t i = 0; i < list->GetSize(); ++i) {
      CefRefPtr<CefDictionaryValue> item = list->GetType(i) == VTYPE_DICTIONARY
                                               ? list->GetDictionary(i)
                                               : nullptr;
      auto field = [&item](const char* key) {
        return item && item->GetType(key) == VTYPE_STRING
                   ? item->GetString(key).ToString()
                   : std::string();
      };
      std::optional<RequestRule::Action> action =
          RequestRuleActionFromName(field("action"));
      if (!item || !action) {
        callback->Failure(kBadRequest, "Invalid request.");
        return true;
      }
      RequestRule rule{
          field("pattern"), *action, field("header"), field("value"),
          item->GetType("enabled") != VTYPE_BOOL || item->GetBool("enabled")};
      if (std::optional<std::string> problem = ValidateRequestRule(rule)) {
        callback->Failure(kFailed,
                          "Rule " + std::to_string(i + 1) + ": " + *problem);
        return true;
      }
      set.rules.push_back(std::move(rule));
    }
    if (std::optional<Error> error = host_->SetRequestRules(std::move(set))) {
      callback->Failure(kFailed, error->message);
    } else {
      callback->Success("");
    }
    return true;
  }
  callback->Failure(kBadRequest, "Invalid request.");
  return true;
}

}  // namespace brouk
