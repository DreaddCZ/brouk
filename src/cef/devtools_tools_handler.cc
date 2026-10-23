#include "cef/devtools_tools_handler.h"

#include <cstddef>
#include <string>

#include "cef/json_util.h"
#include "core/devtools_tools.h"
#include "core/internal_pages.h"
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
  callback->Failure(kBadRequest, "Invalid request.");
  return true;
}

}  // namespace brouk
