#include "core/settings_schema.h"

#include <array>
#include <span>
#include <string_view>

namespace brouk {

namespace {

constexpr std::array<std::string_view, 9> kProfileColors = {
    "grey", "blue",   "red",  "yellow", "green",
    "pink", "purple", "cyan", "orange"};

constexpr std::array<SettingDef, 4> kSettings = {{
    // Global (005).
    {.key = "profiles.default",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "default",
     .max_length = 64,
     .description = "Id of the profile that new windows and tabs open in."},
    {.key = "profiles.show_manager_at_startup",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "false",
     .description = "Show the profile manager when Brouk starts."},
    // Per profile (005).
    {.key = "profile.name",
     .scope = SettingScope::kProfile,
     .type = SettingDef::Type::kString,
     .default_value = "Profile",
     .max_length = 64,
     .description = "The profile's name, shown on its tab groups."},
    {.key = "profile.color",
     .scope = SettingScope::kProfile,
     .type = SettingDef::Type::kChoice,
     .default_value = "blue",
     .choices = kProfileColors,
     .description = "Colour of the profile's tab groups and toolbar."},
}};

}  // namespace

std::span<const SettingDef> AllSettingDefs() {
  return kSettings;
}

const SettingDef* FindSettingDef(SettingScope scope, std::string_view key) {
  for (const SettingDef& def : kSettings) {
    if (def.scope == scope && def.key == key) {
      return &def;
    }
  }
  return nullptr;
}

std::span<const std::string_view> ProfileColorNames() {
  return kProfileColors;
}

}  // namespace brouk
