#include "core/settings_schema.h"

#include <array>
#include <span>
#include <string_view>
#include <vector>

#include "core/shortcuts.h"

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

constexpr std::array<std::string_view, 4> kHistoryKeep = {"30", "90", "365",
                                                          "forever"};
constexpr std::array<std::string_view, 4> kHistoryKeepLabels = {
    "30 days", "90 days", "1 year", "Forever"};

constexpr std::array<std::string_view, 2> kStartup = {"last_session",
                                                      "new_tab"};
constexpr std::array<std::string_view, 2> kStartupLabels = {
    "Continue where you left off", "Open a new tab"};

constexpr std::array<std::string_view, 2> kHistoryViews = {"site", "day"};
constexpr std::array<std::string_view, 2> kHistoryViewLabels = {"By site",
                                                                "By day"};

constexpr std::array<std::string_view, 2> kBookmarksBar = {"new_tab_page",
                                                           "always"};
constexpr std::array<std::string_view, 2> kBookmarksBarLabels = {
    "On the new tab page only", "Always"};

constexpr std::array<SettingDef, 25> kSettings = {{
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
    {.key = "startup.restore",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "last_session",
     .choices = kStartup,
     .choice_labels = kStartupLabels,
     .section = "Startup",
     .label = "On startup",
     .description = "Continue brings back the windows and tabs open when "
                    "Brouk last quit."},
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
    // Address bar suggestions (003); bookmarks join with 011.
    {.key = "suggestions.history",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Search",
     .label = "Suggest pages from your history",
     .description = "In the address bar, from the history of the tab's "
                    "profile."},
    {.key = "suggestions.search",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Search",
     .label = "Show search suggestions",
     .description = "What you type in the address bar is sent to the search "
                    "engine for suggestions."},
    // History (013).
    {.key = "history.keep",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "90",
     .choices = kHistoryKeep,
     .choice_labels = kHistoryKeepLabels,
     .section = "History",
     .label = "Keep history for",
     .description = "Older visits are deleted automatically."},
    {.key = "history.view",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "site",
     .choices = kHistoryViews,
     .choice_labels = kHistoryViewLabels,
     .section = "History",
     .label = "History page view",
     .description = "The history page remembers the last view chosen."},
    {.key = "history.never_record",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .format = SettingDef::Format::kUrlPatterns,
     .max_length = 4096,
     .section = "History",
     .label = "Never record these sites",
     .description = "Comma-separated, e.g. login.microsoftonline.com, "
                    "*.okta.com. Their pages already in the history are "
                    "removed too."},
    // Bookmarks (011).
    {.key = "bookmarks.bar",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "new_tab_page",
     .choices = kBookmarksBar,
     .choice_labels = kBookmarksBarLabels,
     .section = "Bookmarks",
     .label = "Show the bookmarks bar",
     .description = "View > Always Show Bookmarks Bar switches it too."},
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
    {.key = "content.json_viewer",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Pages",
     .label = "Show JSON documents as a tree",
     .description = "With search, collapsing, and copying of values and "
                    "paths."},
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
    {.key = "devtools.secret_headers",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "Authorization, Cookie, x-api-key, api-key, x-auth-token",
     .max_length = 1024,
     .section = "Developer tools",
     .label = "Secret headers in exports",
     .description = "Comma-separated header names whose values become "
                    "variables when requests are exported to Postman or "
                    "Bruno."},
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
    {.key = "history.keep_list",
     .scope = SettingScope::kProfile,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .format = SettingDef::Format::kUrlPatterns,
     .max_length = 16384,
     .advanced = true,
     .section = "History",
     .label = "Clean up keep list",
     .description = "The sites kept the last time the history was cleaned "
                    "up; updated by Clean up."},
}};

// The settings above, then one per shortcut action (003).
std::vector<SettingDef> BuildSettingDefs() {
  std::vector<SettingDef> defs(kSettings.begin(), kSettings.end());
  for (const ShortcutAction& action : AllShortcutActions()) {
    defs.push_back({.key = action.setting_key,
                    .scope = SettingScope::kGlobal,
                    .type = SettingDef::Type::kString,
                    .default_value = action.defaults,
                    .format = SettingDef::Format::kShortcut,
                    .max_length = 200,
                    .section = "Shortcuts",
                    .label = action.label});
  }
  return defs;
}

}  // namespace

std::span<const SettingDef> AllSettingDefs() {
  // Built once; the strings it points to are all constants.
  static const std::vector<SettingDef> defs = BuildSettingDefs();
  return defs;
}

const SettingDef* FindSettingDef(SettingScope scope, std::string_view key) {
  for (const SettingDef& def : AllSettingDefs()) {
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
