#include "core/settings_schema.h"

#include <array>
#include <span>
#include <string_view>

namespace brouk {

namespace {

constexpr std::array<std::string_view, 9> kProfileColors = {
    "grey", "blue",   "red",  "yellow", "green",
    "pink", "purple", "cyan", "orange"};

constexpr std::array<std::string_view, 9> kProfileColorLabels = {
    "Grey", "Blue",   "Red",  "Yellow", "Green",
    "Pink", "Purple", "Cyan", "Orange"};

constexpr std::array<std::string_view, 3> kThemes = {"system", "light", "dark"};
constexpr std::array<std::string_view, 3> kThemeLabels = {"Same as macOS",
                                                          "Light", "Dark"};

constexpr std::array<std::string_view, 6> kSearchEngines = {
    "google", "bing", "duckduckgo", "startpage", "ecosia", "custom"};
constexpr std::array<std::string_view, 6> kSearchEngineLabels = {
    "Google", "Bing", "DuckDuckGo", "Startpage", "Ecosia", "Custom (below)"};

constexpr std::array<std::string_view, 2> kSides = {"right", "left"};
constexpr std::array<std::string_view, 2> kSideLabels = {
    "Right of the address bar", "Left of the address bar"};

constexpr std::array<std::string_view, 4> kDockPositions = {"bottom", "right",
                                                            "left", "window"};
constexpr std::array<std::string_view, 4> kDockPositionLabels = {
    "Bottom", "Right", "Left", "Separate window"};

constexpr std::array<SettingDef, 15> kSettings = {{
    // Global.
    {.key = "appearance.theme",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "system",
     .choices = kThemes,
     .choice_labels = kThemeLabels,
     .section = "Appearance",
     .label = "Theme",
     .description = "Light or dark windows; by default as set in macOS."},
    {.key = "search.engine",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "google",
     .choices = kSearchEngines,
     .choice_labels = kSearchEngineLabels,
     .section = "Search",
     .label = "Search engine",
     .description = "Used for text typed into the address bar that is not "
                    "an address."},
    {.key = "search.custom_template",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .format = SettingDef::Format::kUrlTemplate,
     .max_length = 2048,
     .section = "Search",
     .label = "Custom search address",
     .description = "For \"Custom\": the search page's address with %s "
                    "where the search text goes, e.g. "
                    "https://example.com/search?q=%s"},
    {.key = "downloads.folder",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .format = SettingDef::Format::kFolder,
     .max_length = 1024,
     .section = "Downloads",
     .label = "Download folder",
     .description = "Where downloads are saved. Empty: your Downloads "
                    "folder."},
    {.key = "downloads.ask_where_to_save",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "false",
     .section = "Downloads",
     .label = "Ask where to save each file"},
    {.key = "content.spellcheck",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Pages",
     .label = "Check spelling in text fields",
     .description = "Uses the macOS spelling dictionaries."},
    // Developer tools (006).
    {.key = "devtools.toggle_position",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "right",
     .choices = kSides,
     .choice_labels = kSideLabels,
     .section = "Developer tools",
     .label = "Dev tools button",
     .description = "Where the button that switches a tab's developer tools "
                    "on and off is shown."},
    {.key = "devtools.dock_position",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "bottom",
     .choices = kDockPositions,
     .choice_labels = kDockPositionLabels,
     .section = "Developer tools",
     .label = "Developer tools position",
     .description = "Also changed with the buttons in the developer tools."},
    {.key = "devtools.dock_size",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kInteger,
     .default_value = "320",
     .min_value = 120,
     .max_value = 4000,
     .advanced = true,
     .section = "Developer tools",
     .label = "Developer tools size",
     .description = "Height or width of the docked developer tools in "
                    "points; remembered when resized."},
    {.key = "devtools.remote_debugging",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "false",
     .requires_restart = true,
     .section = "Developer tools",
     .label = "Allow remote debugging",
     .description = "For Playwright, Puppeteer and chrome://inspect, on this "
                    "Mac only. Programs on this Mac can then control tabs of "
                    "all profiles, including their sign-ins."},
    {.key = "devtools.remote_debugging_port",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kInteger,
     .default_value = "9222",
     .min_value = 1024,
     .max_value = 65535,
     .requires_restart = true,
     .section = "Developer tools",
     .label = "Remote debugging port"},
    {.key = "profiles.default",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "default",
     .format = SettingDef::Format::kProfileId,
     .max_length = 64,
     .section = "Profiles",
     .label = "Default profile",
     .description = "New windows and tabs, and links from other apps, open "
                    "in this profile."},
    {.key = "profiles.show_manager_at_startup",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "false",
     .section = "Profiles",
     .label = "Show the profile manager when Brouk starts"},
    // Per profile.
    {.key = "profile.name",
     .scope = SettingScope::kProfile,
     .type = SettingDef::Type::kString,
     .default_value = "Profile",
     .max_length = 64,
     .section = "Profile",
     .label = "Name",
     .description = "Shown on the profile's tab groups."},
    {.key = "profile.color",
     .scope = SettingScope::kProfile,
     .type = SettingDef::Type::kChoice,
     .default_value = "blue",
     .choices = kProfileColors,
     .choice_labels = kProfileColorLabels,
     .section = "Profile",
     .label = "Colour",
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
