#include "cef/adblock_settings.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "cef/settings_handler.h"
#include "core/adblock_engine.h"
#include "core/adblock_lists.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr int kBadRequest = 400;
constexpr int kFailed = 500;
constexpr std::size_t kMaxMyFiltersBytes = std::size_t{1000} * 1024;
constexpr std::size_t kMaxProblems = 200;
constexpr std::size_t kMaxUrlBytes = 2048;

using Callback = CefMessageRouterBrowserSide::Callback;

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::optional<std::string> Field(const CefRefPtr<CefDictionaryValue>& message,
                                 const char* key,
                                 std::size_t max_bytes) {
  if (message->GetType(key) != VTYPE_STRING) {
    return std::nullopt;
  }
  std::string value = message->GetString(key).ToString();
  if (value.size() > max_bytes) {
    return std::nullopt;
  }
  return value;
}

std::vector<std::string> Ids(const std::string& text) {
  std::vector<std::string> ids;
  std::string current;
  for (char c : text + ",") {
    if (c == ',' || c == ' ') {
      if (!current.empty()) {
        ids.push_back(current);
      }
      current.clear();
    } else {
      current += c;
    }
  }
  return ids;
}

std::string JoinIds(const std::vector<std::string>& ids) {
  std::string text;
  for (const std::string& id : ids) {
    text += (text.empty() ? "" : ", ") + id;
  }
  return text;
}

FilterListChoice Choice(const Settings& global) {
  return {.turned_on = Ids(global.GetString("adblock.lists_on")),
          .turned_off = Ids(global.GetString("adblock.lists_off")),
          .custom =
              ParseCustomFilterLists(global.GetString("adblock.custom_lists"))};
}

// Invalid lines of My filters, with 1-based line numbers.
CefRefPtr<CefListValue> Problems(std::string_view text) {
  CefRefPtr<CefListValue> problems = CefListValue::Create();
  std::size_t line_number = 0;
  std::size_t pos = 0;
  while (pos <= text.size() && problems->GetSize() < kMaxProblems) {
    std::size_t end = text.find('\n', pos);
    std::string_view line =
        text.substr(pos, end == std::string_view::npos ? std::string_view::npos
                                                       : end - pos);
    ++line_number;
    FilterLineCheck check = CheckFilterLine(line);
    if (check.kind == FilterLineCheck::Kind::kInvalid) {
      CefRefPtr<CefDictionaryValue> problem = CefDictionaryValue::Create();
      problem->SetInt("line", static_cast<int>(line_number));
      problem->SetString("error", check.error);
      problems->SetDictionary(problems->GetSize(), problem);
    }
    if (end == std::string_view::npos) {
      break;
    }
    pos = end + 1;
  }
  return problems;
}

std::string StateJson(SettingsPageDelegate& delegate) {
  const Settings& global = delegate.Registry().global_settings();
  FilterListChoice choice = Choice(global);
  std::vector<FilterListDef> active =
      ActiveFilterLists(choice, delegate.Language());
  AdblockService* service = delegate.Adblock();
  CefRefPtr<CefListValue> categories = CefListValue::Create();
  for (FilterCategory category : AllFilterCategories()) {
    CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
    item->SetString("id", std::string(FilterCategoryId(category)));
    item->SetString("label", std::string(FilterCategoryLabel(category)));
    categories->SetDictionary(categories->GetSize(), item);
  }
  CefRefPtr<CefListValue> lists = CefListValue::Create();
  auto add = [&](const FilterListDef& list, bool custom) {
    bool enabled = std::ranges::any_of(
        active, [&](const FilterListDef& a) { return a.id == list.id; });
    CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
    item->SetString("id", list.id);
    item->SetString("name", list.name);
    item->SetString("category", std::string(FilterCategoryId(list.category)));
    item->SetString("url", list.url);
    item->SetBool("enabled", enabled);
    item->SetBool("custom", custom);
    double updated = 0;
    std::string error;
    if (service) {
      auto it = service->list_states().find(list.id);
      if (it != service->list_states().end()) {
        updated = static_cast<double>(it->second.updated);
        error = it->second.error;
      }
    }
    item->SetDouble("updated", updated);
    item->SetString("error", error);
    lists->SetDictionary(lists->GetSize(), item);
  };
  for (const FilterListDef& list : BuiltinFilterLists()) {
    add(list, false);
  }
  for (const FilterListDef& list : choice.custom) {
    add(list, true);
  }
  CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
  answer->SetList("categories", categories);
  answer->SetList("lists", lists);
  answer->SetBool("updating", service && service->updating());
  answer->SetBool("ready", service != nullptr);
  return ToJson(answer);
}

void Answer(const CefRefPtr<Callback>& callback,
            const std::optional<Error>& error) {
  if (error) {
    callback->Failure(kFailed, error->message);
  } else {
    callback->Success("");
  }
}

}  // namespace

bool HandleAdblockSettingsMessage(const std::string& type,
                                  const CefRefPtr<CefDictionaryValue>& message,
                                  SettingsPageDelegate& delegate,
                                  const CefRefPtr<Callback>& callback) {
  CEF_REQUIRE_UI_THREAD();
  const Settings& global = delegate.Registry().global_settings();
  if (type == "adblock.state") {
    callback->Success(StateJson(delegate));
    return true;
  }
  if (type == "adblock.setList") {
    std::optional<std::string> id = Field(message, "id", 64);
    bool enabled = message->GetType("enabled") == VTYPE_BOOL &&
                   message->GetBool("enabled");
    FilterListChoice choice = Choice(global);
    bool custom = id && std::ranges::any_of(choice.custom, [&](const auto& l) {
                    return l.id == *id;
                  });
    if (!id || (!custom && !FindBuiltinFilterList(*id))) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    std::erase(choice.turned_on, *id);
    std::erase(choice.turned_off, *id);
    // Recorded only where it differs from the default.
    bool default_on =
        custom || std::ranges::any_of(
                      ActiveFilterLists({}, delegate.Language()),
                      [&](const FilterListDef& l) { return l.id == *id; });
    if (enabled && !default_on) {
      choice.turned_on.push_back(*id);
    } else if (!enabled && default_on) {
      choice.turned_off.push_back(*id);
    }
    std::optional<Error> error = delegate.SetGlobalSetting(
        "adblock.lists_on", SettingValue(JoinIds(choice.turned_on)));
    if (!error) {
      error = delegate.SetGlobalSetting(
          "adblock.lists_off", SettingValue(JoinIds(choice.turned_off)));
    }
    Answer(callback, error);
    return true;
  }
  if (type == "adblock.addList") {
    std::optional<std::string> category = Field(message, "category", 32);
    std::optional<std::string> url = Field(message, "url", kMaxUrlBytes);
    if (!category || !url || !FilterCategoryFromId(*category)) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    std::string entry = *category + "=" + *url;
    std::vector<FilterListDef> parsed = ParseCustomFilterLists(entry);
    if (parsed.empty() || url->find_first_of(" \t\r\n") != std::string::npos) {
      callback->Failure(kFailed, "Enter the list's address (https://…).");
      return true;
    }
    FilterListChoice choice = Choice(global);
    if (std::ranges::any_of(choice.custom, [&](const FilterListDef& l) {
          return l.id == parsed[0].id;
        })) {
      callback->Failure(kFailed, "This list is already added.");
      return true;
    }
    choice.custom.push_back(parsed[0]);
    Answer(callback, delegate.SetGlobalSetting(
                         "adblock.custom_lists",
                         SettingValue(FormatCustomFilterLists(choice.custom))));
    return true;
  }
  if (type == "adblock.removeList") {
    std::optional<std::string> id = Field(message, "id", 64);
    FilterListChoice choice = Choice(global);
    std::size_t before = choice.custom.size();
    if (id) {
      std::erase_if(choice.custom,
                    [&](const FilterListDef& l) { return l.id == *id; });
    }
    if (!id || choice.custom.size() == before) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    std::erase(choice.turned_off, *id);
    std::optional<Error> error = delegate.SetGlobalSetting(
        "adblock.custom_lists",
        SettingValue(FormatCustomFilterLists(choice.custom)));
    if (!error) {
      error = delegate.SetGlobalSetting(
          "adblock.lists_off", SettingValue(JoinIds(choice.turned_off)));
    }
    Answer(callback, error);
    return true;
  }
  if (type == "adblock.update") {
    if (AdblockService* service = delegate.Adblock()) {
      service->UpdateNow();
    }
    callback->Success("");
    return true;
  }
  if (type == "adblock.checkFilters") {
    std::optional<std::string> text =
        Field(message, "text", kMaxMyFiltersBytes);
    if (!text) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetList("problems", Problems(*text));
    callback->Success(ToJson(answer));
    return true;
  }
  if (type == "adblock.myFilters") {
    AdblockService* service = delegate.Adblock();
    if (!service) {
      callback->Failure(kFailed, "Ad blocking is still starting.");
      return true;
    }
    CefRefPtr<Callback> answer_to = callback;
    service->ReadMyFilters([answer_to](std::string text) {
      CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
      answer->SetString("text", text);
      answer->SetList("problems", Problems(text));
      answer_to->Success(ToJson(answer));
    });
    return true;
  }
  if (type == "adblock.saveMyFilters") {
    AdblockService* service = delegate.Adblock();
    std::optional<std::string> text =
        Field(message, "text", kMaxMyFiltersBytes);
    if (!service || !text) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    CefRefPtr<CefListValue> problems = Problems(*text);
    CefRefPtr<Callback> answer_to = callback;
    service->SaveMyFilters(*text, [answer_to, problems](bool saved) {
      if (!saved) {
        answer_to->Failure(kFailed, "My filters could not be saved.");
        return;
      }
      CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
      answer->SetList("problems", problems);
      answer_to->Success(ToJson(answer));
    });
    return true;
  }
  return false;
}

}  // namespace brouk
