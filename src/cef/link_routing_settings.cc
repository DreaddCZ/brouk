#include "cef/link_routing_settings.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cef/profile_links.h"
#include "cef/settings_handler.h"
#include "core/link_routing_rules.h"
#include "core/link_unwrap.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr int kBadRequest = 400;
constexpr int kFailed = 500;
constexpr std::size_t kMaxEntries = 500;
constexpr std::size_t kMaxTextBytes = 2048;

using Callback = CefMessageRouterBrowserSide::Callback;

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::optional<std::string> Text(const CefRefPtr<CefDictionaryValue>& item,
                                const char* key) {
  if (!item || item->GetType(key) != VTYPE_STRING) {
    return std::nullopt;
  }
  std::string value = item->GetString(key).ToString();
  if (value.size() > kMaxTextBytes) {
    return std::nullopt;
  }
  return value;
}

CefRefPtr<CefListValue> List(const CefRefPtr<CefDictionaryValue>& item,
                             const char* key) {
  if (!item || item->GetType(key) != VTYPE_LIST) {
    return nullptr;
  }
  CefRefPtr<CefListValue> list = item->GetList(key);
  return list->GetSize() <= kMaxEntries ? list : nullptr;
}

CefRefPtr<CefListValue> StringList(const std::vector<std::string>& items) {
  CefRefPtr<CefListValue> list = CefListValue::Create();
  for (std::size_t i = 0; i < items.size(); ++i) {
    list->SetString(i, items[i]);
  }
  return list;
}

CefRefPtr<CefDictionaryValue> StateJson(SettingsPageDelegate& delegate) {
  CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
  const LinkRoutingConfig* config = delegate.LinkRouting();
  answer->SetBool("ready", config != nullptr);
  CefRefPtr<CefListValue> rules = CefListValue::Create();
  CefRefPtr<CefListValue> wrappers = CefListValue::Create();
  if (config) {
    for (const RoutingRule& rule : config->rules) {
      CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
      item->SetString("pattern", rule.pattern);
      item->SetList("profiles", StringList(rule.profiles));
      rules->SetDictionary(rules->GetSize(), item);
    }
    for (const LinkWrapperRule& wrapper : config->wrappers) {
      CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
      item->SetString("pattern", wrapper.pattern);
      item->SetString("parameter", wrapper.parameter);
      wrappers->SetDictionary(wrappers->GetSize(), item);
    }
  }
  answer->SetList("rules", rules);
  answer->SetList("wrappers", wrappers);
  CefRefPtr<CefListValue> builtins = CefListValue::Create();
  for (const BuiltInLinkWrapper& builtin : BuiltInLinkWrappers()) {
    CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
    item->SetString("name", std::string(builtin.name));
    item->SetString("hosts", std::string(builtin.hosts));
    item->SetBool("opaque", builtin.opaque);
    builtins->SetDictionary(builtins->GetSize(), item);
  }
  answer->SetList("builtins", builtins);
  CefRefPtr<CefListValue> profiles = CefListValue::Create();
  for (const ProfileInfo& info : delegate.Registry().profiles()) {
    CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
    item->SetString("id", info.id);
    item->SetString("name", info.name);
    item->SetString("color", info.color);
    profiles->SetDictionary(profiles->GetSize(), item);
  }
  answer->SetList("profiles", profiles);
  answer->SetList("problems", StringList(delegate.LinkRoutingProblems()));
  answer->SetBool("isDefaultBrowser", delegate.IsDefaultBrowser());
  return answer;
}

// One problem of a saved entry, for the row that caused it.
void AddProblem(CefRefPtr<CefListValue>& problems,
                const char* kind,
                std::size_t index,
                const std::string& message) {
  CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
  item->SetString("kind", kind);
  item->SetInt("index", static_cast<int>(index));
  item->SetString("message", message);
  problems->SetDictionary(problems->GetSize(), item);
}

// "links.save": the whole list of rules and wrappers. Nothing is saved
// while any entry has a problem.
void Save(const CefRefPtr<CefDictionaryValue>& message,
          SettingsPageDelegate& delegate,
          const CefRefPtr<Callback>& callback) {
  CefRefPtr<CefListValue> rules = List(message, "rules");
  CefRefPtr<CefListValue> wrappers = List(message, "wrappers");
  if (!rules || !wrappers || !delegate.LinkRouting()) {
    callback->Failure(kBadRequest, "Invalid request.");
    return;
  }
  std::vector<ProfileId> existing;
  for (const ProfileInfo& info : delegate.Registry().profiles()) {
    existing.push_back(info.id);
  }
  // Rules may keep profiles deleted meanwhile; new ones must exist.
  const LinkRoutingConfig& current = *delegate.LinkRouting();
  auto known = [&](const ProfileId& id) {
    if (std::ranges::find(existing, id) != existing.end()) {
      return true;
    }
    return std::ranges::any_of(current.rules, [&](const RoutingRule& rule) {
      return std::ranges::find(rule.profiles, id) != rule.profiles.end();
    });
  };
  LinkRoutingConfig config;
  CefRefPtr<CefListValue> problems = CefListValue::Create();
  for (std::size_t i = 0; i < rules->GetSize(); ++i) {
    CefRefPtr<CefDictionaryValue> item = rules->GetType(i) == VTYPE_DICTIONARY
                                             ? rules->GetDictionary(i)
                                             : nullptr;
    std::optional<std::string> pattern = Text(item, "pattern");
    CefRefPtr<CefListValue> profiles = List(item, "profiles");
    if (!pattern || !profiles) {
      callback->Failure(kBadRequest, "Invalid request.");
      return;
    }
    RoutingRule rule;
    rule.pattern = *pattern;
    for (std::size_t k = 0; k < profiles->GetSize(); ++k) {
      if (profiles->GetType(k) != VTYPE_STRING) {
        callback->Failure(kBadRequest, "Invalid request.");
        return;
      }
      rule.profiles.push_back(profiles->GetString(k).ToString());
    }
    std::optional<std::string> problem = ValidateRoutingRule(rule);
    if (!problem && !std::ranges::all_of(rule.profiles, known)) {
      problem = "a profile of this rule does not exist";
    }
    if (problem) {
      AddProblem(problems, "rule", i, *problem);
    }
    config.rules.push_back(std::move(rule));
  }
  for (std::size_t i = 0; i < wrappers->GetSize(); ++i) {
    CefRefPtr<CefDictionaryValue> item =
        wrappers->GetType(i) == VTYPE_DICTIONARY ? wrappers->GetDictionary(i)
                                                 : nullptr;
    std::optional<std::string> pattern = Text(item, "pattern");
    std::optional<std::string> parameter = Text(item, "parameter");
    if (!pattern || !parameter) {
      callback->Failure(kBadRequest, "Invalid request.");
      return;
    }
    LinkWrapperRule wrapper{*pattern, *parameter};
    if (std::optional<std::string> problem = ValidateLinkWrapperRule(wrapper)) {
      AddProblem(problems, "wrapper", i, *problem);
    }
    config.wrappers.push_back(std::move(wrapper));
  }
  CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
  if (problems->GetSize() == 0) {
    if (std::optional<Error> error = delegate.SetLinkRouting(config)) {
      callback->Failure(kFailed, error->message);
      return;
    }
  }
  answer->SetList("problems", problems);
  callback->Success(ToJson(answer));
}

}  // namespace

bool HandleLinkRoutingSettingsMessage(
    const std::string& type,
    const CefRefPtr<CefDictionaryValue>& message,
    SettingsPageDelegate& delegate,
    const CefRefPtr<Callback>& callback) {
  CEF_REQUIRE_UI_THREAD();
  if (type == "links.state") {
    callback->Success(ToJson(StateJson(delegate)));
    return true;
  }
  if (type == "links.save") {
    Save(message, delegate, callback);
    return true;
  }
  if (type == "links.setProfileLinks") {
    // "Links that open in this profile" on the settings page (as in the
    // profile manager).
    std::optional<std::string> profile = Text(message, "profile");
    const LinkRoutingConfig* current = delegate.LinkRouting();
    bool exists = profile && IsValidProfileId(*profile) &&
                  delegate.Registry().Find(*profile) != nullptr;
    if (!exists || !current || message->GetType("patterns") != VTYPE_LIST) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    SaveProfileLinks(
        message->GetList("patterns"), *profile, *current,
        [&delegate](LinkRoutingConfig config) {
          return delegate.SetLinkRouting(std::move(config));
        },
        callback);
    return true;
  }
  if (type == "links.makeDefault") {
    delegate.MakeDefaultBrowser(
        [answer = callback](std::optional<Error> error) {
          if (error) {
            answer->Failure(kFailed, error->message);
          } else {
            answer->Success("");
          }
        });
    return true;
  }
  return false;
}

}  // namespace brouk
