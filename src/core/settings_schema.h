#ifndef BROUK_CORE_SETTINGS_SCHEMA_H_
#define BROUK_CORE_SETTINGS_SCHEMA_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace brouk {

// Global settings are in Brouk/settings.yaml, a profile's settings in
// Brouk/profiles/<id>/settings.yaml (spec 004).
enum class SettingScope { kGlobal, kProfile };

// One setting: its key, type, default and limits. Every setting the browser
// reads is listed here, so values in the files are checked against their
// type, and mistakes (unknown keys, `yes` instead of `true`) are reported
// instead of silently ignored.
struct SettingDef {
  enum class Type { kBool, kString, kChoice, kInteger };
  // What a kString holds; checked when the value is set.
  enum class Format {
    kText,         // A single line of text.
    kUrlTemplate,  // An http(s) URL containing %s (a search engine).
    kFolder,       // Empty (the default folder), an absolute path or ~/...
    kProfileId,    // A profile id (the settings page offers the profiles).
    kShortcut,     // Keyboard shortcuts, e.g. "cmd+shift+t, ctrl+tab" (003).
  };

  // Dotted path; "profile.color" is `color:` under `profile:` in the file.
  std::string_view key;
  SettingScope scope = SettingScope::kGlobal;
  Type type = Type::kBool;
  // As written in the file: "true"/"false", the text, or one of `choices`.
  std::string_view default_value;
  std::span<const std::string_view> choices = {};  // kChoice
  // kChoice: what the settings page shows for each choice (same order).
  std::span<const std::string_view> choice_labels = {};
  Format format = Format::kText;  // kString
  std::size_t max_length = 0;     // kString, in bytes
  std::int64_t min_value = 0;     // kInteger
  std::int64_t max_value = 0;     // kInteger
  bool requires_restart = false;
  // Only in the file, not on the settings page (004: "like about:config").
  bool advanced = false;
  // Settings page grouping and wording.
  std::string_view section = {};
  std::string_view label = {};
  std::string_view description = {};
};

std::span<const SettingDef> AllSettingDefs();
// Null if there is no such setting in `scope`.
const SettingDef* FindSettingDef(SettingScope scope, std::string_view key);

// Colours a profile can have (its tab group and toolbar colour, 005); the
// names of Chrome's tab group colours.
std::span<const std::string_view> ProfileColorNames();

}  // namespace brouk

#endif  // BROUK_CORE_SETTINGS_SCHEMA_H_
