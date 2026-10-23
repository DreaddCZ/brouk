#include "cef/devtools_channel.h"

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "cef/json_util.h"
#include "include/cef_parser.h"
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
  std::string json = ToJson(message);
  if (!tab_->GetHost()->SendDevToolsMessage(json.data(), json.size())) {
    Done failed = std::move(pending_[id]);
    pending_.erase(id);
    failed(nullptr, "The command could not be sent.");
  }
}

bool DevToolsChannel::OnDevToolsMessage(CefRefPtr<CefBrowser> /*browser*/,
                                        const void* message,
                                        size_t message_size) {
  CEF_REQUIRE_UI_THREAD();
  std::string_view text(static_cast<const char*>(message), message_size);
  // Only answers to our commands: {"id":<ours>,...}. Everything else is for
  // the DevTools frontend.
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
