#include "cef/autofill_settings.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cef/autofill_manager.h"
#include "cef/settings_handler.h"
#include "core/autofill_rules.h"
#include "core/autofill_store.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr int kBadRequest = 400;
constexpr int kFailed = 500;
constexpr std::size_t kMaxValueBytes = 4096;
constexpr std::size_t kMaxIds = 10000;
constexpr std::size_t kMaxImportBytes = std::size_t{16} * 1024 * 1024;
constexpr double kMaxId = 9.0e15;

using Callback = CefMessageRouterBrowserSide::Callback;

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::optional<std::int64_t> NumberOf(const CefRefPtr<CefValue>& value) {
  if (value->GetType() == VTYPE_INT) {
    return value->GetInt();
  }
  if (value->GetType() == VTYPE_DOUBLE) {
    double number = value->GetDouble();
    if (number >= 0 && number < kMaxId &&
        number == static_cast<double>(static_cast<std::int64_t>(number))) {
      return static_cast<std::int64_t>(number);
    }
  }
  return std::nullopt;
}

std::optional<std::vector<std::int64_t>> Ids(
    const CefRefPtr<CefDictionaryValue>& message,
    const char* key) {
  if (message->GetType(key) != VTYPE_LIST ||
      message->GetList(key)->GetSize() > kMaxIds) {
    return std::nullopt;
  }
  CefRefPtr<CefListValue> list = message->GetList(key);
  std::vector<std::int64_t> ids;
  for (std::size_t i = 0; i < list->GetSize(); ++i) {
    std::optional<std::int64_t> id = NumberOf(list->GetValue(i));
    if (!id) {
      return std::nullopt;
    }
    ids.push_back(*id);
  }
  return ids;
}

std::optional<std::string> Text(const CefRefPtr<CefDictionaryValue>& message,
                                const char* key) {
  if (message->GetType(key) != VTYPE_STRING) {
    return std::nullopt;
  }
  std::string value = message->GetString(key).ToString();
  if (value.size() > kMaxValueBytes) {
    return std::nullopt;
  }
  return value;
}

CefRefPtr<CefDictionaryValue> EntryJson(const AutofillEntry& entry) {
  CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
  row->SetDouble("id", static_cast<double>(entry.id));
  row->SetString("label", entry.label);
  CefRefPtr<CefDictionaryValue> values = CefDictionaryValue::Create();
  CefRefPtr<CefDictionaryValue> problems = CefDictionaryValue::Create();
  for (std::size_t i = 0; i < kAutofillStoredFields; ++i) {
    auto field = static_cast<AutofillField>(i);
    if (entry.values[i].empty()) {
      continue;
    }
    std::string name(AutofillFieldName(field));
    values->SetString(name, entry.values[i]);
    if (std::optional<std::string> problem =
            ValidateAutofillValue(field, entry.values[i])) {
      problems->SetString(name, *problem);
    }
  }
  row->SetDictionary("values", values);
  row->SetDictionary("problems", problems);
  row->SetString("originSite", entry.origin_site);
  row->SetDouble("created", static_cast<double>(entry.created));
  row->SetDouble("lastUsed", static_cast<double>(entry.last_used));
  row->SetDouble("timesUsed", static_cast<double>(entry.times_used));
  return row;
}

// The entry in a message: {"label": …, "values": {"first_name": …}}.
std::optional<AutofillEntry> EntryFrom(
    const CefRefPtr<CefDictionaryValue>& message) {
  std::optional<std::string> label = Text(message, "label");
  if (!label || message->GetType("values") != VTYPE_DICTIONARY) {
    return std::nullopt;
  }
  AutofillEntry entry;
  entry.label = *label;
  CefRefPtr<CefDictionaryValue> values = message->GetDictionary("values");
  CefDictionaryValue::KeyList keys;
  values->GetKeys(keys);
  for (const CefString& key : keys) {
    std::optional<AutofillField> field = AutofillFieldFromName(key.ToString());
    if (!field || static_cast<std::size_t>(*field) >= kAutofillStoredFields ||
        values->GetType(key) != VTYPE_STRING) {
      return std::nullopt;
    }
    std::string value = values->GetString(key).ToString();
    if (value.size() > kMaxValueBytes) {
      return std::nullopt;
    }
    entry.Set(*field, value);
  }
  return entry;
}

}  // namespace

bool HandleAutofillSettingsMessage(const std::string& type,
                                   const CefRefPtr<CefDictionaryValue>& message,
                                   SettingsPageDelegate& delegate,
                                   const CefRefPtr<Callback>& callback) {
  CEF_REQUIRE_UI_THREAD();
  AutofillManager* manager = delegate.Autofill();
  if (!manager) {
    callback->Failure(kFailed, "Autofill is not ready yet.");
    return true;
  }
  auto fail = [&callback](const std::string& why) {
    callback->Failure(kBadRequest, why);
    return true;
  };
  std::optional<std::int64_t> id =
      message->HasKey("id") ? NumberOf(message->GetValue("id")) : std::nullopt;

  if (type == "autofill.state") {
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetBool("loaded", manager->loaded());
    answer->SetString("error", manager->error().value_or(""));
    answer->SetString("country", manager->options().country);
    CefRefPtr<CefListValue> entries = CefListValue::Create();
    for (const AutofillEntry& entry : manager->entries()) {
      entries->SetDictionary(entries->GetSize(), EntryJson(entry));
    }
    answer->SetList("entries", entries);
    CefRefPtr<CefListValue> custom = CefListValue::Create();
    for (const CustomField& field : manager->custom_fields()) {
      CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
      row->SetDouble("id", static_cast<double>(field.id));
      row->SetString("name", field.name);
      row->SetString("value", field.value);
      CefRefPtr<CefListValue> words = CefListValue::Create();
      for (const std::string& word : field.label_words) {
        words->SetString(words->GetSize(), word);
      }
      row->SetList("labelWords", words);
      row->SetDouble("lastUsed", static_cast<double>(field.last_used));
      custom->SetDictionary(custom->GetSize(), row);
    }
    answer->SetList("custom", custom);
    answer->SetBool("canUndo", manager->CanUndo());
    CefRefPtr<CefListValue> groups = CefListValue::Create();
    answer->SetList("groups", groups);
    callback->Success(ToJson(answer));
    return true;
  }
  if (type == "autofill.saveEntry") {
    std::optional<AutofillEntry> entry = EntryFrom(message);
    if (!id || !entry) {
      return fail("Invalid request.");
    }
    if (entry->Empty()) {
      return fail("Fill in at least one field.");
    }
    if (*id == 0) {
      manager->AddEntry(std::move(*entry),
                        [callback](const Result<std::int64_t>& added) {
                          if (added) {
                            callback->Success("");
                          } else {
                            callback->Failure(kFailed, added.error().message);
                          }
                        });
      return true;
    }
    auto old = std::ranges::find_if(
        manager->entries(),
        [&](const AutofillEntry& e) { return e.id == *id; });
    if (old == manager->entries().end()) {
      return fail("The entry no longer exists.");
    }
    entry->id = *id;
    entry->origin_site = old->origin_site;
    manager->UpdateEntry(*entry);
    callback->Success("");
    return true;
  }
  if (type == "autofill.deleteEntries" || type == "autofill.deleteCustom") {
    std::optional<std::vector<std::int64_t>> ids = Ids(message, "ids");
    if (!ids) {
      return fail("Invalid request.");
    }
    if (type == "autofill.deleteEntries") {
      manager->RemoveEntries(*ids);
    } else {
      manager->RemoveCustomFields(*ids);
    }
    callback->Success("");
    return true;
  }
  if (type == "autofill.undo") {
    manager->Undo();
    callback->Success("");
    return true;
  }
  if (type == "autofill.saveCustom") {
    std::optional<std::string> name = Text(message, "name");
    std::optional<std::string> value = Text(message, "value");
    if (!id || !name || !value ||
        message->GetType("labelWords") != VTYPE_LIST) {
      return fail("Invalid request.");
    }
    CustomField field;
    field.id = *id;
    field.name = *name;
    field.value = *value;
    CefRefPtr<CefListValue> words = message->GetList("labelWords");
    for (std::size_t i = 0; i < words->GetSize() && i < 50; ++i) {
      if (words->GetType(i) != VTYPE_STRING) {
        return fail("Invalid request.");
      }
      std::string word = words->GetString(i).ToString();
      if (!word.empty() && word.size() <= 200) {
        field.label_words.push_back(word);
      }
    }
    if (field.name.empty()) {
      return fail("Give the field a name.");
    }
    if (*id == 0) {
      manager->AddCustomField(
          std::move(field), [callback](const Result<std::int64_t>& added) {
            if (added) {
              callback->Success("");
            } else {
              callback->Failure(kFailed, added.error().message);
            }
          });
    } else {
      manager->UpdateCustomField(field);
      callback->Success("");
    }
    return true;
  }
  if (type == "autofill.duplicates") {
    CefRefPtr<CefListValue> groups = CefListValue::Create();
    const std::vector<AutofillEntry>& entries = manager->entries();
    for (const std::vector<std::size_t>& group :
         FindDuplicateEntries(entries, manager->options().country)) {
      CefRefPtr<CefListValue> ids = CefListValue::Create();
      for (std::size_t index : group) {
        ids->SetDouble(ids->GetSize(), static_cast<double>(entries[index].id));
      }
      groups->SetList(groups->GetSize(), ids);
    }
    CefRefPtr<CefValue> value = CefValue::Create();
    value->SetList(groups);
    callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString());
    return true;
  }
  if (type == "autofill.merge") {
    // The merged values go to `id`; the other entries of the group go.
    std::optional<AutofillEntry> entry = EntryFrom(message);
    std::optional<std::vector<std::int64_t>> remove = Ids(message, "remove");
    if (!id || !entry || !remove) {
      return fail("Invalid request.");
    }
    auto keep = std::ranges::find_if(
        manager->entries(),
        [&](const AutofillEntry& e) { return e.id == *id; });
    if (keep == manager->entries().end()) {
      return fail("The entry no longer exists.");
    }
    entry->id = *id;
    entry->origin_site = keep->origin_site;
    std::erase(*remove, *id);
    manager->UpdateEntry(*entry);
    manager->RemoveEntries(*remove);
    callback->Success("");
    return true;
  }
  if (type == "autofill.export") {
    delegate.SaveAutofillFile(
        WriteAutofillJson(manager->entries(), manager->custom_fields()),
        [callback](bool saved, const std::optional<Error>& error) {
          if (error) {
            callback->Failure(kFailed, error->message);
          } else {
            callback->Success(saved ? "true" : "false");
          }
        });
    return true;
  }
  if (type == "autofill.import") {
    delegate.OpenAutofillFile(
        [manager, callback](const std::optional<std::string>& text,
                            const std::optional<Error>& error) {
          if (error) {
            callback->Failure(kFailed, error->message);
            return;
          }
          if (!text) {
            callback->Success("null");
            return;
          }
          if (text->size() > kMaxImportBytes) {
            callback->Failure(kFailed, "The file is too large.");
            return;
          }
          Result<AutofillJson> parsed = ParseAutofillJson(*text);
          if (!parsed) {
            callback->Failure(kFailed, parsed.error().message);
            return;
          }
          AutofillImportPlan plan = PlanAutofillImport(
              parsed.value(), manager->entries(), manager->custom_fields(),
              manager->options().country);
          for (const AutofillEntry& entry : plan.merged) {
            manager->UpdateEntry(entry);
          }
          for (AutofillEntry& entry : plan.to_add) {
            manager->AddEntry(std::move(entry), nullptr);
          }
          for (CustomField& field : plan.custom_to_add) {
            manager->AddCustomField(std::move(field), nullptr);
          }
          CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
          answer->SetInt("added", static_cast<int>(plan.to_add.size()));
          answer->SetInt("merged", static_cast<int>(plan.merged.size()));
          answer->SetInt("skipped", plan.skipped);
          answer->SetInt("customAdded",
                         static_cast<int>(plan.custom_to_add.size()));
          answer->SetInt("customSkipped", plan.custom_skipped);
          callback->Success(ToJson(answer));
        });
    return true;
  }
  return false;
}

}  // namespace brouk
