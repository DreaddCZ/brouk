#include "cef/help_page_handler.h"

#include <map>
#include <string>
#include <vector>

#include "cef/json_util.h"
#include "core/internal_pages.h"
#include "core/settings_schema.h"
#include "core/shortcuts.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

CefRefPtr<CefListValue> ShortcutsJson(const Settings& global) {
  std::vector<std::string> problems;
  std::map<std::string, std::vector<Shortcut>, std::less<>> current =
      ResolveShortcuts(global, problems);
  CefRefPtr<CefListValue> list = CefListValue::Create();
  for (const ShortcutAction& action : AllShortcutActions()) {
    CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
    row->SetString("id", std::string(action.id));
    row->SetString("label", std::string(action.label));
    row->SetString("setting", std::string(action.setting_key));
    CefRefPtr<CefListValue> keys = CefListValue::Create();
    if (auto it = current.find(action.id); it != current.end()) {
      for (const Shortcut& shortcut : it->second) {
        keys->SetString(keys->GetSize(), DisplayShortcut(shortcut));
      }
    }
    row->SetList("keys", keys);
    list->SetDictionary(list->GetSize(), row);
  }
  return list;
}

std::string TypeName(SettingDef::Type type) {
  switch (type) {
    case SettingDef::Type::kBool:
      return "on/off";
    case SettingDef::Type::kString:
      return "text";
    case SettingDef::Type::kChoice:
      return "choice";
    case SettingDef::Type::kInteger:
      return "number";
  }
  return "text";
}

// The settings page's settings (not the advanced ones, 004); shortcuts
// have their own section.
CefRefPtr<CefListValue> SettingsJson() {
  CefRefPtr<CefListValue> list = CefListValue::Create();
  for (const SettingDef& def : AllSettingDefs()) {
    if (def.advanced || def.key.starts_with("shortcuts.")) {
      continue;
    }
    CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
    row->SetString("key", std::string(def.key));
    row->SetString("label", std::string(def.label));
    row->SetString("description", std::string(def.description));
    row->SetString("section", std::string(def.section));
    row->SetString("scope",
                   def.scope == SettingScope::kGlobal ? "global" : "profile");
    row->SetString("type", TypeName(def.type));
    row->SetString("default", std::string(def.default_value));
    CefRefPtr<CefListValue> choices = CefListValue::Create();
    for (std::size_t i = 0; i < def.choices.size(); ++i) {
      CefRefPtr<CefDictionaryValue> choice = CefDictionaryValue::Create();
      choice->SetString("value", std::string(def.choices[i]));
      choice->SetString("label", i < def.choice_labels.size()
                                     ? std::string(def.choice_labels[i])
                                     : std::string(def.choices[i]));
      choices->SetDictionary(choices->GetSize(), choice);
    }
    row->SetList("choices", choices);
    list->SetDictionary(list->GetSize(), row);
  }
  return list;
}

}  // namespace

bool HelpPageHandler::OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                              CefRefPtr<CefFrame> frame,
                              int64_t /*query_id*/,
                              const CefString& request,
                              bool persistent,
                              CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame || InternalPageOf(frame->GetURL().ToString()) != "help") {
    return false;
  }
  std::string text = request.ToString();
  CefRefPtr<CefValue> parsed = text.size() <= 4096 ? ParseJson(text) : nullptr;
  CefRefPtr<CefDictionaryValue> message =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  if (persistent || !message ||
      message->GetString("type").ToString() != "help.state") {
    callback->Failure(400, "Invalid request.");
    return true;
  }
  CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
  answer->SetList("shortcuts", ShortcutsJson(global_settings_()));
  answer->SetList("settings", SettingsJson());
  callback->Success(ToJson(answer));
  return true;
}

}  // namespace brouk
