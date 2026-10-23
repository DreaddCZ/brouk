#include "cef/devtools_frontend_client.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "cef/json_util.h"
#include "core/devtools_frontend.h"
#include "core/external_protocol.h"
#include "include/cef_parser.h"
#include "include/cef_process_message.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// Ids of our own protocol commands start here (see next_own_id_).
constexpr int kOwnIdBase = 1'000'000'000;
// Messages from the frontend renderer (it is ours, but limit anyway).
constexpr std::size_t kMaxFrontendMessageBytes = std::size_t{64} * 1024 * 1024;
constexpr std::size_t kMaxCopyBytes = std::size_t{16} * 1024 * 1024;
// Large protocol messages reach the frontend in chunks, as in Chrome.
constexpr std::size_t kChunkBytes = std::size_t{2} * 1024 * 1024;

CefRefPtr<CefValue> StringValue(std::string_view text) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetString(std::string(text));
  return value;
}

CefRefPtr<CefValue> DictValue(const CefRefPtr<CefDictionaryValue>& dict) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dict);
  return value;
}

std::string ToJson(const CefRefPtr<CefValue>& value) {
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

// The "id" of a protocol message, if it starts with one ({"id":123,...}),
// without parsing the (possibly large) rest.
std::optional<long long> LeadingId(std::string_view message) {
  constexpr std::string_view kPrefix = R"({"id":)";
  if (!message.starts_with(kPrefix)) {
    return std::nullopt;
  }
  message.remove_prefix(kPrefix.size());
  long long id = 0;
  auto [end, error] =
      std::from_chars(message.data(), message.data() + message.size(), id);
  if (error != std::errc()) {
    return std::nullopt;
  }
  return id;
}

// Where to cut `text` at most `limit` bytes from `start` without splitting a
// UTF-8 character.
std::size_t ChunkEnd(std::string_view text,
                     std::size_t start,
                     std::size_t limit) {
  std::size_t end = std::min(text.size(), start + limit);
  while (end < text.size() && end > start &&
         (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
    --end;
  }
  return end;
}

}  // namespace

DevToolsFrontendClient::DevToolsFrontendClient(
    DevToolsFrontendDelegate* delegate,
    CefRefPtr<CefBrowser> inspected,
    DevToolsPreferences* preferences)
    : delegate_(delegate),
      inspected_(std::move(inspected)),
      preferences_(preferences) {}

void DevToolsFrontendClient::DetachDelegate() {
  CEF_REQUIRE_UI_THREAD();
  delegate_ = nullptr;
  registration_ = nullptr;
  inspected_ = nullptr;
  after_load_.clear();
  own_requests_.clear();
}

void DevToolsFrontendClient::InspectAt(int x, int y) {
  CEF_REQUIRE_UI_THREAD();
  // Find the node at once: opening the dock resizes the page, and then
  // something else would be at (x, y). Both commands go out together.
  SendToPage("DOM.getDocument", nullptr, nullptr);
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetInt("x", x);
  params->SetInt("y", y);
  params->SetBool("includeUserAgentShadowDOM", false);
  params->SetBool("ignorePointerEventsNone", true);
  CefRefPtr<DevToolsFrontendClient> self(this);
  SendToPage("DOM.getNodeForLocation", params,
             [self](const CefRefPtr<CefDictionaryValue>& result) {
               if (!result || result->GetType("backendNodeId") != VTYPE_INT) {
                 return;
               }
               int node = result->GetInt("backendNodeId");
               // As Chrome's InspectElement: the backend reports the node as if
               // the user had picked it, and the frontend reveals it in
               // Elements.
               self->WhenLoaded([self, node] {
                 CefRefPtr<CefDictionaryValue> event_params =
                     CefDictionaryValue::Create();
                 event_params->SetInt("backendNodeId", node);
                 CefRefPtr<CefDictionaryValue> event =
                     CefDictionaryValue::Create();
                 event->SetString("method", "Overlay.inspectNodeRequested");
                 event->SetDictionary("params", event_params);
                 self->ForwardToFrontend(ToJson(DictValue(event)));
               });
             });
}

void DevToolsFrontendClient::ShowPanel(const std::string& panel) {
  CEF_REQUIRE_UI_THREAD();
  if (panel != "console" && panel != "elements") {
    return;
  }
  CefRefPtr<DevToolsFrontendClient> self(this);
  WhenLoaded([self, panel] {
    self->CallFrontend("DevToolsAPI.showPanel", {StringValue(panel)});
  });
}

void DevToolsFrontendClient::EnterInspectElementMode() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<DevToolsFrontendClient> self(this);
  WhenLoaded([self] {
    self->CallFrontend("DevToolsAPI.enterInspectElementMode", {});
  });
}

void DevToolsFrontendClient::Close() {
  CEF_REQUIRE_UI_THREAD();
  ResetPage();
  registration_ = nullptr;
  if (frontend_) {
    frontend_->GetHost()->CloseBrowser(true);
  }
}

void DevToolsFrontendClient::ResetPage() {
  if (!inspected_ || !registration_) {
    return;
  }
  // Chrome detaches the frontend's session, which resets all of this; the
  // CEF session stays attached for the tab's lifetime, so undo it here.
  constexpr auto kResets = std::to_array<std::pair<const char*, const char*>>({
      {"Debugger.disable", "{}"},
      {"Overlay.setInspectMode", R"({"mode":"none","highlightConfig":{}})"},
      {"Overlay.hideHighlight", "{}"},
      {"Emulation.clearDeviceMetricsOverride", "{}"},
      {"Emulation.setTouchEmulationEnabled", R"({"enabled":false})"},
      {"Emulation.setEmulatedMedia", R"({"media":"","features":[]})"},
      {"Emulation.setEmulatedVisionDeficiency", R"({"type":"none"})"},
      {"Emulation.setCPUThrottlingRate", R"({"rate":1})"},
      {"Emulation.clearGeolocationOverride", "{}"},
      {"Network.emulateNetworkConditions",
       R"({"offline":false,"latency":0,"downloadThroughput":-1,"uploadThroughput":-1})"},
      {"Network.setCacheDisabled", R"({"cacheDisabled":false})"},
      {"Network.setBlockedURLs", R"({"urls":[]})"},
      {"Network.disable", "{}"},
      {"Profiler.disable", "{}"},
      {"CSS.disable", "{}"},
      {"DOM.disable", "{}"},
      {"Overlay.disable", "{}"},
      {"Log.disable", "{}"},
      {"Runtime.disable", "{}"},
  });
  for (const auto& [method, params] : kResets) {
    CefRefPtr<CefValue> parsed = ParseJson(params);
    SendToPage(method, parsed ? parsed->GetDictionary() : nullptr, nullptr);
  }
}

bool DevToolsFrontendClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  if (source_process != PID_RENDERER ||
      message->GetName().ToString() != kDevToolsHostMessage) {
    return false;
  }
  // Only the frontend page itself (the browser's own view of the frame).
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  if (!frame || !frame->IsMain() ||
      !IsDevToolsFrontendUrl(frame->GetURL().ToString()) || !args ||
      args->GetSize() != 2 || args->GetType(0) != VTYPE_STRING ||
      args->GetType(1) != VTYPE_STRING || !delegate_) {
    return true;
  }
  std::string kind = args->GetString(0).ToString();
  std::string payload = args->GetString(1).ToString();
  if (kind == kDevToolsEmbedderKind &&
      payload.size() <= kMaxFrontendMessageBytes) {
    HandleEmbedderMessage(payload);
  } else if (kind == kDevToolsCopyKind && payload.size() <= kMaxCopyBytes) {
    delegate_->CopyTextFromDevTools(payload);
  }
  return true;
}

void DevToolsFrontendClient::HandleEmbedderMessage(const std::string& json) {
  CefRefPtr<CefValue> parsed = ParseJson(json);
  CefRefPtr<CefDictionaryValue> message =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  if (!message || message->GetType("method") != VTYPE_STRING) {
    return;
  }
  std::string method = message->GetString("method").ToString();
  int id = message->GetType("id") == VTYPE_INT ? message->GetInt("id") : 0;
  CefRefPtr<CefListValue> params = message->GetType("params") == VTYPE_LIST
                                       ? message->GetList("params")
                                       : CefListValue::Create();
  auto string_param = [&params](std::size_t index) -> std::string {
    return params->GetSize() > index && params->GetType(index) == VTYPE_STRING
               ? params->GetString(index).ToString()
               : std::string();
  };

  CefRefPtr<CefValue> result;
  if (method == "dispatchProtocolMessage") {
    std::string protocol = string_param(0);
    if (inspected_ && !protocol.empty()) {
      if (!registration_) {
        registration_ = inspected_->GetHost()->AddDevToolsMessageObserver(this);
      }
      inspected_->GetHost()->SendDevToolsMessage(protocol.data(),
                                                 protocol.size());
    }
  } else if (method == "loadCompleted") {
    // As Content Shell: DevTools draws its own context menus.
    CefRefPtr<CefValue> yes = CefValue::Create();
    yes->SetBool(true);
    CallFrontend("DevToolsAPI.setUseSoftMenu", {yes});
    loaded_ = true;
    std::vector<std::function<void()>> actions = std::move(after_load_);
    after_load_.clear();
    for (const auto& action : actions) {
      action();
    }
  } else if (method == "getPreferences") {
    result = DictValue(preferences_->All());
  } else if (method == "getPreference") {
    result = preferences_->Get(string_param(0));
  } else if (method == "setPreference") {
    preferences_->Set(string_param(0), string_param(1));
  } else if (method == "removePreference") {
    preferences_->Remove(string_param(0));
  } else if (method == "clearPreferences") {
    preferences_->Clear();
  } else if (method == "getHostConfig") {
    result = DictValue(CefDictionaryValue::Create());
  } else if (method == "getSyncInformation") {
    CefRefPtr<CefDictionaryValue> sync = CefDictionaryValue::Create();
    sync->SetBool("isSyncActive", false);
    sync->SetBool("arePreferencesSynced", false);
    result = DictValue(sync);
  } else if (method == "canShowSurvey") {
    CefRefPtr<CefDictionaryValue> survey = CefDictionaryValue::Create();
    survey->SetBool("canShowSurvey", false);
    result = DictValue(survey);
  } else if (method == "showSurvey") {
    CefRefPtr<CefDictionaryValue> survey = CefDictionaryValue::Create();
    survey->SetBool("surveyShown", false);
    result = DictValue(survey);
  } else if (method == "connectAutomaticFileSystem") {
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetBool("success", false);
    result = DictValue(answer);
  } else if (method == "loadNetworkResource") {
    // Source maps from the network are not loaded (yet).
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetInt("statusCode", 404);
    answer->SetBool("urlValid", true);
    answer->SetInt("netError", -2);
    answer->SetString("netErrorName", "net::ERR_FAILED");
    answer->SetDictionary("headers", CefDictionaryValue::Create());
    result = DictValue(answer);
  } else if (method == "dispatchHttpRequest" ||
             method == "doAidaConversation" || method == "aidaCodeComplete" ||
             method == "registerAidaClientEvent") {
    // Google's online services for DevTools: not in Brouk.
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetString("error", "Not available in Brouk.");
    result = DictValue(answer);
  } else if (method == "requestFileSystems") {
    CefRefPtr<CefValue> none = CefValue::Create();
    none->SetList(CefListValue::Create());
    CallFrontend("DevToolsAPI.fileSystemsLoaded", {none});
  } else if (method == "save") {
    // Saving files from Sources: not yet.
    CallFrontend("DevToolsAPI.canceledSaveURL", {StringValue(string_param(0))});
  } else if (method == "closeWindow") {
    if (delegate_) {
      delegate_->OnDevToolsWantsClose();
    }
  } else if (method == "openInNewTab") {
    std::string url = string_param(0);
    std::string scheme = SchemeOf(url);
    if (delegate_ && (scheme == "http" || scheme == "https")) {
      delegate_->OpenUrlFromDevTools(url);
    }
  }
  // Every other method (window placement, metrics, ...) needs no action.
  if (id != 0) {
    Ack(id, result);
  }
}

void DevToolsFrontendClient::Ack(int id, const CefRefPtr<CefValue>& result) {
  CefRefPtr<CefValue> id_value = CefValue::Create();
  id_value->SetInt(id);
  if (result) {
    CallFrontend("DevToolsAPI.embedderMessageAck", {id_value, result});
  } else {
    CallFrontend("DevToolsAPI.embedderMessageAck", {id_value});
  }
}

void DevToolsFrontendClient::CallFrontend(
    std::string_view function,
    const std::vector<CefRefPtr<CefValue>>& args) {
  if (!frontend_) {
    return;
  }
  std::string script(function);
  script += '(';
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i > 0) {
      script += ',';
    }
    // JSON is valid JavaScript for these values.
    script += ToJson(args[i]);
  }
  script += ')';
  frontend_->GetMainFrame()->ExecuteJavaScript(
      script, std::string(kDevToolsFrontendUrl), 0);
}

void DevToolsFrontendClient::ForwardToFrontend(std::string_view message) {
  if (message.size() <= kChunkBytes) {
    CallFrontend("DevToolsAPI.dispatchMessage", {StringValue(message)});
    return;
  }
  CefRefPtr<CefValue> total = CefValue::Create();
  total->SetInt(static_cast<int>(message.size()));
  for (std::size_t pos = 0; pos < message.size();) {
    std::size_t end = ChunkEnd(message, pos, kChunkBytes);
    CefRefPtr<CefValue> chunk = StringValue(message.substr(pos, end - pos));
    if (pos == 0) {
      CallFrontend("DevToolsAPI.dispatchMessageChunk", {chunk, total});
    } else {
      CallFrontend("DevToolsAPI.dispatchMessageChunk", {chunk});
    }
    pos = end;
  }
}

void DevToolsFrontendClient::SendToPage(
    const std::string& method,
    const CefRefPtr<CefDictionaryValue>& params,
    PageResult done) {
  if (!inspected_) {
    return;
  }
  if (!registration_) {
    registration_ = inspected_->GetHost()->AddDevToolsMessageObserver(this);
  }
  int id = next_own_id_++;
  CefRefPtr<CefDictionaryValue> message = CefDictionaryValue::Create();
  message->SetInt("id", id);
  message->SetString("method", method);
  message->SetDictionary("params",
                         params ? params : CefDictionaryValue::Create());
  if (done) {
    own_requests_[id] = std::move(done);
  }
  std::string json = ToJson(DictValue(message));
  inspected_->GetHost()->SendDevToolsMessage(json.data(), json.size());
}

void DevToolsFrontendClient::WhenLoaded(std::function<void()> action) {
  if (loaded_) {
    action();
  } else {
    after_load_.push_back(std::move(action));
  }
}

bool DevToolsFrontendClient::OnDevToolsMessage(
    CefRefPtr<CefBrowser> /*browser*/,
    const void* message,
    size_t message_size) {
  CEF_REQUIRE_UI_THREAD();
  std::string_view text(static_cast<const char*>(message), message_size);
  std::optional<long long> id = LeadingId(text);
  if (id && *id >= kOwnIdBase) {
    auto it = own_requests_.find(static_cast<int>(*id));
    if (it != own_requests_.end()) {
      PageResult done = std::move(it->second);
      own_requests_.erase(it);
      CefRefPtr<CefValue> parsed = ParseJson(text);
      CefRefPtr<CefDictionaryValue> dict =
          parsed && parsed->GetType() == VTYPE_DICTIONARY
              ? parsed->GetDictionary()
              : nullptr;
      done(dict && dict->GetType("result") == VTYPE_DICTIONARY
               ? dict->GetDictionary("result")
               : nullptr);
    }
    return true;  // Ours: never shown to the frontend.
  }
  if (frontend_) {
    ForwardToFrontend(text);
  }
  return true;
}

bool DevToolsFrontendClient::OnKeyEvent(CefRefPtr<CefBrowser> /*browser*/,
                                        const CefKeyEvent& event,
                                        CefEventHandle os_event) {
  CEF_REQUIRE_UI_THREAD();
  return delegate_ && delegate_->OnDevToolsKeyEvent(event, os_event);
}

bool DevToolsFrontendClient::OnBeforePopup(
    CefRefPtr<CefBrowser> /*browser*/,
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
    bool* /*no_javascript_access*/) {
  CEF_REQUIRE_UI_THREAD();
  return true;  // Links go through openInNewTab.
}

void DevToolsFrontendClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  frontend_ = browser;
  if (delegate_) {
    delegate_->OnDevToolsCreated(browser);
  }
}

bool DevToolsFrontendClient::DoClose(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnDevToolsCloseReady();
  }
  return true;
}

void DevToolsFrontendClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  frontend_ = nullptr;
  registration_ = nullptr;
  if (delegate_) {
    delegate_->OnDevToolsClosed();
  }
}

bool DevToolsFrontendClient::OnBeforeBrowse(CefRefPtr<CefBrowser> /*browser*/,
                                            CefRefPtr<CefFrame> /*frame*/,
                                            CefRefPtr<CefRequest> request,
                                            bool /*user_gesture*/,
                                            bool /*is_redirect*/) {
  CEF_REQUIRE_UI_THREAD();
  // The dock shows the DevTools frontend and nothing else.
  return !IsDevToolsFrontendUrl(request->GetURL().ToString());
}

}  // namespace brouk
