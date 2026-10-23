#include "cef/devtools_preferences.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// The file is ours, but limit what is read anyway.
constexpr std::size_t kMaxFileBytes = std::size_t{4} * 1024 * 1024;
constexpr std::size_t kMaxValueBytes = std::size_t{1} * 1024 * 1024;

}  // namespace

DevToolsPreferences::DevToolsPreferences(std::string path)
    : path_(std::move(path)) {}

void DevToolsPreferences::Load() {
  if (values_) {
    return;
  }
  values_ = CefDictionaryValue::Create();
  std::error_code error;
  if (std::filesystem::file_size(path_, error) > kMaxFileBytes || error) {
    return;
  }
  std::ifstream file(path_, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  CefRefPtr<CefValue> parsed = CefParseJSON(text, JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) {
    return;
  }
  CefRefPtr<CefDictionaryValue> stored = parsed->GetDictionary();
  CefDictionaryValue::KeyList keys;
  stored->GetKeys(keys);
  for (const CefString& key : keys) {
    if (stored->GetType(key) == VTYPE_STRING) {
      values_->SetString(key, stored->GetString(key));
    }
  }
}

void DevToolsPreferences::Save() {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(values_->Copy(false));
  std::string text = CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
  std::string temp = path_ + ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file << text;
  }
  std::error_code error;
  std::filesystem::permissions(
      temp,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      error);
  std::filesystem::rename(temp, path_, error);
}

CefRefPtr<CefDictionaryValue> DevToolsPreferences::All() {
  CEF_REQUIRE_UI_THREAD();
  Load();
  return values_->Copy(false);
}

CefRefPtr<CefValue> DevToolsPreferences::Get(const std::string& name) {
  CEF_REQUIRE_UI_THREAD();
  Load();
  return values_->HasKey(name) ? values_->GetValue(name) : nullptr;
}

void DevToolsPreferences::Set(const std::string& name,
                              const std::string& value) {
  CEF_REQUIRE_UI_THREAD();
  Load();
  if (name.empty() || name.size() > 1024 || value.size() > kMaxValueBytes) {
    return;
  }
  values_->SetString(name, value);
  Save();
}

void DevToolsPreferences::Remove(const std::string& name) {
  CEF_REQUIRE_UI_THREAD();
  Load();
  values_->Remove(name);
  Save();
}

void DevToolsPreferences::Clear() {
  CEF_REQUIRE_UI_THREAD();
  Load();
  values_->Clear();
  Save();
}

}  // namespace brouk
