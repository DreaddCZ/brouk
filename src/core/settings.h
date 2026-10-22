#ifndef BROUK_CORE_SETTINGS_H_
#define BROUK_CORE_SETTINGS_H_

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/settings_schema.h"
#include "core/yaml_value.h"

namespace brouk {

// kBool settings hold bool, kString and kChoice settings hold std::string,
// kInteger settings hold std::int64_t.
using SettingValue = std::variant<bool, std::string, std::int64_t>;

// Something wrong in a settings file. The setting keeps its default.
struct SettingProblem {
  std::string key;  // Empty for problems with the whole file.
  int line = 0;     // 0 if unknown.
  std::string message;

  // "line 3, profile.color: must be one of grey, blue, …"
  std::string ToString() const;
};

// The values of all settings of one scope (global or one profile), always
// valid: anything invalid is replaced by the default and reported.
class Settings {
 public:
  explicit Settings(SettingScope scope);  // All defaults.

  // Reads the values in a parsed settings file; `problems` gets everything
  // that was wrong (unknown keys, wrong types, values out of range).
  static Settings FromYaml(SettingScope scope,
                           const YamlNode& root,
                           std::vector<SettingProblem>& problems);
  // All settings of the scope, nested by their dotted keys.
  YamlNode ToYaml() const;

  SettingScope scope() const { return scope_; }

  // `key` must be a setting of this scope with that type.
  bool GetBool(std::string_view key) const;
  const std::string& GetString(std::string_view key) const;
  std::int64_t GetInt(std::string_view key) const;

  // Checks and sets a value; returns the problem if it is not valid (the
  // setting then keeps its value).
  std::optional<std::string> Set(std::string_view key, SettingValue value);

  bool operator==(const Settings& other) const = default;

 private:
  SettingScope scope_;
  std::map<std::string, SettingValue, std::less<>> values_;
};

// Checks a value against its definition; returns the problem, if any.
std::optional<std::string> ValidateSetting(const SettingDef& def,
                                           const SettingValue& value);

}  // namespace brouk

#endif  // BROUK_CORE_SETTINGS_H_
