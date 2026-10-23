#include "cef/settings_handler.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/internal_pages.h"
#include "core/profile_id.h"
#include "core/settings_schema.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxRequestBytes = std::size_t{16} * 1024;
constexpr std::size_t kMaxFieldBytes = 4096;
constexpr int kBadRequest = 400;
constexpr int kFailed = 500;

std::string_view TypeName(SettingDef::Type type) {
  switch (type) {
    case SettingDef::Type::kBool:
      return "bool";
    case SettingDef::Type::kString:
      return "string";
    case SettingDef::Type::kChoice:
      return "choice";
  }
  return "string";
}

std::string_view FormatName(SettingDef::Format format) {
  switch (format) {
    case SettingDef::Format::kText:
      return "text";
    case SettingDef::Format::kUrlTemplate:
      return "urlTemplate";
    case SettingDef::Format::kFolder:
      return "folder";
    case SettingDef::Format::kProfileId:
      return "profileId";
  }
  return "text";
}

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

// The settings of one scope as the page shows them (non-advanced only).
CefRefPtr<CefListValue> Items(SettingScope scope, const Settings& values) {
  CefRefPtr<CefListValue> items = CefListValue::Create();
  std::size_t index = 0;
  for (const SettingDef& def : AllSettingDefs()) {
    if (def.scope != scope || def.advanced) {
      continue;
    }
    CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
    item->SetString("key", std::string(def.key));
    item->SetString("type", std::string(TypeName(def.type)));
    item->SetString("format", std::string(FormatName(def.format)));
    item->SetString("section", std::string(def.section));
    item->SetString("label", std::string(def.label));
    item->SetString("description", std::string(def.description));
    item->SetBool("restart", def.requires_restart);
    CefRefPtr<CefListValue> choices = CefListValue::Create();
    for (std::size_t i = 0; i < def.choices.size(); ++i) {
      CefRefPtr<CefDictionaryValue> choice = CefDictionaryValue::Create();
      choice->SetString("value", std::string(def.choices[i]));
      choice->SetString("label", std::string(i < def.choice_labels.size()
                                                 ? def.choice_labels[i]
                                                 : def.choices[i]));
      choices->SetDictionary(i, choice);
    }
    item->SetList("choices", choices);
    if (def.type == SettingDef::Type::kBool) {
      item->SetBool("value", values.GetBool(def.key));
    } else {
      item->SetString("value", values.GetString(def.key));
    }
    items->SetDictionary(index++, item);
  }
  return items;
}

std::string StateJson(SettingsPageDelegate& delegate,
                      const std::optional<std::string>& profile_id) {
  const ProfileRegistry& registry = delegate.Registry();
  CefRefPtr<CefDictionaryValue> state = CefDictionaryValue::Create();
  state->SetList("global",
                 Items(SettingScope::kGlobal, registry.global_settings()));

  CefRefPtr<CefListValue> profiles = CefListValue::Create();
  std::size_t index = 0;
  for (const ProfileInfo& info : registry.profiles()) {
    CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
    item->SetString("id", info.id);
    item->SetString("name", info.name);
    profiles->SetDictionary(index++, item);
  }
  state->SetList("profiles", profiles);

  std::string id = profile_id && registry.Find(*profile_id)
                       ? *profile_id
                       : registry.default_profile().id;
  CefRefPtr<CefDictionaryValue> profile = CefDictionaryValue::Create();
  profile->SetString("id", id);
  std::optional<Settings> values = delegate.ProfileSettings(id);
  profile->SetList("items",
                   Items(SettingScope::kProfile,
                         values ? *values : Settings(SettingScope::kProfile)));
  state->SetDictionary("profile", profile);

  CefRefPtr<CefListValue> problems = CefListValue::Create();
  index = 0;
  for (const std::string& problem : registry.problems()) {
    problems->SetString(index++, problem);
  }
  state->SetList("problems", problems);
  return ToJson(state);
}

std::optional<std::string> StringField(const CefRefPtr<CefDictionaryValue>& d,
                                       const char* key) {
  if (d->GetType(key) != VTYPE_STRING) {
    return std::nullopt;
  }
  std::string value = d->GetString(key).ToString();
  if (value.size() > kMaxFieldBytes) {
    return std::nullopt;
  }
  return value;
}

// The new value for `def` from the message, if it has the right JSON type.
std::optional<SettingValue> ValueField(const CefRefPtr<CefDictionaryValue>& d,
                                       const SettingDef& def) {
  if (def.type == SettingDef::Type::kBool) {
    if (d->GetType("value") != VTYPE_BOOL) {
      return std::nullopt;
    }
    return SettingValue(d->GetBool("value"));
  }
  std::optional<std::string> text = StringField(d, "value");
  if (!text) {
    return std::nullopt;
  }
  return SettingValue(*text);
}

SettingsPageDelegate::FileDone FileAnswer(
    const CefRefPtr<CefMessageRouterBrowserSide::Callback>& callback) {
  return [callback](bool finished, std::vector<std::string> problems,
                    std::optional<Error> error) {
    if (error) {
      callback->Failure(kFailed, error->message);
      return;
    }
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetBool("finished", finished);
    CefRefPtr<CefListValue> list = CefListValue::Create();
    for (std::size_t i = 0; i < problems.size(); ++i) {
      list->SetString(i, problems[i]);
    }
    answer->SetList("problems", list);
    callback->Success(ToJson(answer));
  };
}

}  // namespace

bool SettingsHandler::OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                              CefRefPtr<CefFrame> frame,
                              int64_t /*query_id*/,
                              const CefString& request,
                              bool persistent,
                              CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  // Only the settings page itself (the browser's own view of the frame).
  if (!frame || InternalPageOf(frame->GetURL().ToString()) != "settings") {
    return false;
  }
  std::string text = request.ToString();
  CefRefPtr<CefValue> parsed = text.size() <= kMaxRequestBytes && !persistent
                                   ? CefParseJSON(text, JSON_PARSER_RFC)
                                   : nullptr;
  CefRefPtr<CefDictionaryValue> message =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  std::optional<std::string> type =
      message ? StringField(message, "type") : std::nullopt;
  if (!type) {
    callback->Failure(kBadRequest, "Invalid request.");
    return true;
  }
  SettingsPageDelegate& delegate = *delegate_;

  if (*type == "settings.state") {
    std::optional<std::string> profile = StringField(message, "profile");
    if (profile && !IsValidProfileId(*profile)) {
      profile.reset();
    }
    callback->Success(StateJson(delegate, profile));
  } else if (*type == "settings.set") {
    std::optional<std::string> scope = StringField(message, "scope");
    std::optional<std::string> key = StringField(message, "key");
    bool global = scope == "global";
    const SettingDef* def =
        key && (global || scope == "profile")
            ? FindSettingDef(
                  global ? SettingScope::kGlobal : SettingScope::kProfile, *key)
            : nullptr;
    std::optional<SettingValue> value =
        def && !def->advanced ? ValueField(message, *def) : std::nullopt;
    std::optional<std::string> profile =
        global ? std::nullopt : StringField(message, "profile");
    if (!key || !value ||
        (!global && (!profile || !IsValidProfileId(*profile)))) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    std::optional<Error> error =
        global || !profile ? delegate.SetGlobalSetting(*key, *value)
                           : delegate.SetProfileSetting(*profile, *key, *value);
    if (error) {
      // The message names the problem; the page shows it under the setting.
      callback->Failure(kFailed, error->message);
      return true;
    }
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetBool("restart", def->requires_restart);
    callback->Success(ToJson(answer));
  } else if (*type == "settings.openFolder") {
    delegate.RevealSettingsFolder();
    callback->Success("");
  } else if (*type == "settings.export") {
    delegate.ExportSettings(FileAnswer(callback));
  } else if (*type == "settings.import") {
    delegate.ImportSettings(FileAnswer(callback));
  } else {
    callback->Failure(kBadRequest, "Unknown request.");
  }
  return true;
}

}  // namespace brouk
