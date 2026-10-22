#include "core/settings.h"

#include <cassert>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brouk {

namespace {

SettingValue DefaultValue(const SettingDef& def) {
  if (def.type == SettingDef::Type::kBool) {
    return def.default_value == "true";
  }
  return std::string(def.default_value);
}

std::string ChoiceList(const SettingDef& def) {
  std::string list;
  for (std::string_view choice : def.choices) {
    if (!list.empty()) {
      list += ", ";
    }
    list += choice;
  }
  return list;
}

// The value of a scalar in the file, or the problem with it.
std::variant<SettingValue, std::string> ValueFromYaml(const SettingDef& def,
                                                      const YamlNode& node) {
  if (node.kind != YamlNode::Kind::kScalar) {
    return std::string(node.kind == YamlNode::Kind::kNull
                           ? "has no value"
                           : "must be a single value");
  }
  if (def.type == SettingDef::Type::kBool) {
    // Only YAML 1.2's true/false: YAML 1.1 parsers would also read yes, no,
    // on and off as booleans, which is easy to get wrong (004, Notes).
    if (node.plain && (node.scalar == "true" || node.scalar == "false")) {
      return SettingValue(node.scalar == "true");
    }
    return std::string("must be true or false (without quotes)");
  }
  return SettingValue(node.scalar);
}

void Flatten(const YamlNode& map,
             const std::string& prefix,
             std::vector<std::pair<std::string, const YamlNode*>>& out) {
  for (const YamlEntry& entry : map.entries) {
    std::string key = prefix.empty() ? entry.key : prefix + "." + entry.key;
    if (entry.value.kind == YamlNode::Kind::kMap) {
      Flatten(entry.value, key, out);
    } else {
      out.emplace_back(std::move(key), &entry.value);
    }
  }
}

}  // namespace

std::string SettingProblem::ToString() const {
  std::string text;
  if (line > 0) {
    text = "line " + std::to_string(line);
  }
  if (!key.empty()) {
    text += (text.empty() ? "" : ", ") + key;
  }
  return text.empty() ? message : text + ": " + message;
}

std::optional<std::string> ValidateSetting(const SettingDef& def,
                                           const SettingValue& value) {
  switch (def.type) {
    case SettingDef::Type::kBool:
      if (!std::holds_alternative<bool>(value)) {
        return "must be true or false";
      }
      return std::nullopt;
    case SettingDef::Type::kString: {
      const std::string* text = std::get_if<std::string>(&value);
      if (!text) {
        return "must be text";
      }
      if (def.max_length > 0 && text->size() > def.max_length) {
        return "must be at most " + std::to_string(def.max_length) +
               " bytes long";
      }
      for (char c : *text) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
          return std::string(
              "must not contain line breaks or control "
              "characters");
        }
      }
      return std::nullopt;
    }
    case SettingDef::Type::kChoice: {
      const std::string* text = std::get_if<std::string>(&value);
      if (text) {
        for (std::string_view choice : def.choices) {
          if (*text == choice) {
            return std::nullopt;
          }
        }
      }
      return "must be one of " + ChoiceList(def);
    }
  }
  return "unknown setting type";
}

Settings::Settings(SettingScope scope) : scope_(scope) {
  for (const SettingDef& def : AllSettingDefs()) {
    if (def.scope == scope) {
      values_.emplace(std::string(def.key), DefaultValue(def));
    }
  }
}

Settings Settings::FromYaml(SettingScope scope,
                            const YamlNode& root,
                            std::vector<SettingProblem>& problems) {
  Settings settings(scope);
  if (root.kind == YamlNode::Kind::kNull) {
    return settings;  // An empty file: all defaults.
  }
  if (root.kind != YamlNode::Kind::kMap) {
    problems.push_back({{},
                        root.line,
                        "the file must contain settings, "
                        "one \"key: value\" per line"});
    return settings;
  }
  std::vector<std::pair<std::string, const YamlNode*>> entries;
  Flatten(root, {}, entries);
  for (const auto& [key, node] : entries) {
    const SettingDef* def = FindSettingDef(scope, key);
    if (!def) {
      problems.push_back({key, node->line, "unknown setting"});
      continue;
    }
    auto parsed = ValueFromYaml(*def, *node);
    if (const std::string* problem = std::get_if<std::string>(&parsed)) {
      problems.push_back({key, node->line, *problem});
      continue;
    }
    if (std::optional<std::string> problem =
            settings.Set(key, std::get<SettingValue>(std::move(parsed)))) {
      problems.push_back({key, node->line, *problem});
    }
  }
  return settings;
}

YamlNode Settings::ToYaml() const {
  YamlNode root = YamlNode::Map();
  for (const SettingDef& def : AllSettingDefs()) {
    if (def.scope != scope_) {
      continue;
    }
    // "a.b.c" -> a: {b: {c: value}}
    YamlNode* node = &root;
    std::string_view key = def.key;
    for (std::size_t dot = key.find('.'); dot != std::string_view::npos;
         dot = key.find('.')) {
      node = &node->GetOrAdd(key.substr(0, dot));
      node->kind = YamlNode::Kind::kMap;
      key.remove_prefix(dot + 1);
    }
    const SettingValue& value = values_.find(def.key)->second;
    if (const bool* flag = std::get_if<bool>(&value)) {
      node->GetOrAdd(key) = YamlNode::Scalar(*flag ? "true" : "false", true);
    } else {
      // Choices are plain words; free text is quoted so it reads back as
      // text whatever it contains.
      node->GetOrAdd(key) = YamlNode::Scalar(
          std::get<std::string>(value), def.type == SettingDef::Type::kChoice);
    }
  }
  return root;
}

bool Settings::GetBool(std::string_view key) const {
  auto it = values_.find(key);
  assert(it != values_.end() && std::holds_alternative<bool>(it->second));
  const bool* value =
      it == values_.end() ? nullptr : std::get_if<bool>(&it->second);
  return value && *value;
}

const std::string& Settings::GetString(std::string_view key) const {
  static const std::string kEmpty;
  auto it = values_.find(key);
  assert(it != values_.end() &&
         std::holds_alternative<std::string>(it->second));
  const std::string* value =
      it == values_.end() ? nullptr : std::get_if<std::string>(&it->second);
  return value ? *value : kEmpty;
}

std::optional<std::string> Settings::Set(std::string_view key,
                                         SettingValue value) {
  const SettingDef* def = FindSettingDef(scope_, key);
  if (!def) {
    return "unknown setting";
  }
  if (std::optional<std::string> problem = ValidateSetting(*def, value)) {
    return problem;
  }
  values_.find(key)->second = std::move(value);
  return std::nullopt;
}

}  // namespace brouk
