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

constexpr std::array<std::string_view, 2> kOtherSpace = {"switch", "move"};
constexpr std::array<std::string_view, 2> kOtherSpaceLabels = {
    "Switch to that Space", "Move the window here"};

constexpr std::array<std::string_view, 2> kHistoryViews = {"site", "day"};
constexpr std::array<std::string_view, 2> kHistoryViewLabels = {"By site",
                                                                "By day"};

constexpr std::array<std::string_view, 2> kBookmarksBar = {"new_tab_page",
                                                           "always"};
constexpr std::array<std::string_view, 2> kBookmarksBarLabels = {
    "On the new tab page only", "Always"};

constexpr std::array<std::string_view, 2> kPasswordSources = {"builtin",
                                                              "bitwarden"};
constexpr std::array<std::string_view, 2> kPasswordSourceLabels = {
    "Built-in password manager", "Bitwarden"};

constexpr std::array<std::string_view, 7> kSecureDns = {
    "off", "cloudflare", "quad9", "adguard", "nextdns", "controld", "custom"};
constexpr std::array<std::string_view, 7> kSecureDnsLabels = {
    "Off (system DNS)", "Cloudflare", "Quad9", "AdGuard DNS (blocks ads)",
    "NextDNS",          "Control D",  "Custom"};

constexpr std::array<std::string_view, 2> kUpdateChannels = {"stable", "beta"};
constexpr std::array<std::string_view, 2> kUpdateChannelLabels = {
    "Stable", "Beta (early versions for testing)"};

constexpr std::array<std::string_view, 3> kUpdateModes = {"install", "notify",
                                                          "off"};
constexpr std::array<std::string_view, 3> kUpdateModeLabels = {
    "Download and install automatically", "Ask before downloading",
    "Do not check automatically"};

constexpr std::array<std::string_view, 5> kTaskRefresh = {"0.5s", "1s", "2s",
                                                          "5s", "10s"};
constexpr std::array<std::string_view, 5> kTaskRefreshLabels = {
    "Every half second", "Every second", "Every 2 seconds", "Every 5 seconds",
    "Every 10 seconds"};

constexpr std::array<SettingDef, 58> kSettings = {{
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
    // Address bar suggestions (003, 011).
    {.key = "suggestions.history",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Search",
     .label = "Suggest pages from your history",
     .description = "In the address bar, from the history of the tab's "
                    "profile."},
    {.key = "suggestions.bookmarks",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Search",
     .label = "Suggest bookmarks",
     .description = "In the address bar, bookmarks shown in the tab's "
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
    // Ad and tracker blocking (007): which lists (global).
    {.key = "adblock.lists_on",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .max_length = 4096,
     .advanced = true,
     .section = "Ad blocking",
     .label = "Lists turned on",
     .description = "Ids of lists turned on besides the default ones."},
    {.key = "adblock.lists_off",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .max_length = 4096,
     .advanced = true,
     .section = "Ad blocking",
     .label = "Lists turned off",
     .description = "Ids of default lists (and lists added by URL) turned "
                    "off."},
    {.key = "adblock.custom_lists",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .max_length = 16384,
     .advanced = true,
     .section = "Ad blocking",
     .label = "Lists added by URL",
     .description = "category=URL entries separated by spaces."},
    // Secure DNS (007): app-wide, as Chromium configures DNS for the whole
    // network service.
    {.key = "dns.secure",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "off",
     .choices = kSecureDns,
     .choice_labels = kSecureDnsLabels,
     .section = "Secure DNS",
     .label = "Look up addresses with DNS over HTTPS",
     .description = "For all profiles. Company VPN and intranet names often "
                    "resolve only through the company's DNS and may stop "
                    "working."},
    {.key = "dns.custom_template",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .max_length = 1024,
     .section = "Secure DNS",
     .label = "Custom DNS over HTTPS address",
     .description = "For \"Custom\": the service's address, e.g. "
                    "https://dns.nextdns.io/abc123 for your own NextDNS "
                    "configuration."},
    // Password manager (008).
    {.key = "passwords.fill_on_click",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "false",
     .section = "Passwords",
     .label = "Fill passwords only on click",
     .description = "Otherwise a site's only saved login is filled when the "
                    "page loads."},
    {.key = "passwords.generated_length",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kInteger,
     .default_value = "20",
     .min_value = 8,
     .max_value = 64,
     .section = "Passwords",
     .label = "Length of generated passwords"},
    {.key = "passwords.generated_characters",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "upper, lower, digits, symbols",
     .max_length = 64,
     .section = "Passwords",
     .label = "Characters in generated passwords",
     .description = "Any of upper, lower, digits, symbols."},
    {.key = "passwords.copy_totp",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "false",
     .section = "Passwords",
     .label = "Copy the 2FA code after filling a login",
     .description = "For logins with a 2FA secret."},
    {.key = "passwords.breach_check",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Passwords",
     .label = "Check saved passwords for breaches",
     .description = "Sends only the first 5 characters of each password's "
                    "hash to Have I Been Pwned."},
    {.key = "passwords.bitwarden_cli",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .max_length = 1024,
     .section = "Passwords",
     .label = "Bitwarden CLI",
     .description = "Path of the bw program. Empty: found automatically."},
    {.key = "passwords.bitwarden_lock_minutes",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kInteger,
     .default_value = "15",
     .min_value = 1,
     .max_value = 1440,
     .section = "Passwords",
     .label = "Lock Bitwarden after (minutes)",
     .description = "Without use. It also locks when the Mac sleeps or the "
                    "screen locks."},
    {.key = "passwords.bitwarden_touch_id",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Passwords",
     .label = "Unlock Bitwarden with Touch ID"},
    // Form autofill (009), for all profiles.
    {.key = "autofill.enabled",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Autofill",
     .label = "Fill forms with saved addresses and details"},
    {.key = "autofill.entries",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Autofill",
     .label = "Offer saved entries (contact, address, company)"},
    {.key = "autofill.custom_fields",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Autofill",
     .label = "Offer custom fields"},
    {.key = "autofill.offer_to_save",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Autofill",
     .label = "Offer to save what you type into forms"},
    {.key = "autofill.phone_country",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .max_length = 2,
     .section = "Autofill",
     .label = "Country for phone numbers",
     .description = "Two letters, e.g. CZ. Numbers without a country code "
                    "are compared as numbers of this country. Empty: the "
                    "Mac's region."},
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
    // Link routing (010).
    {.key = "links.other_space",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "switch",
     .choices = kOtherSpace,
     .choice_labels = kOtherSpaceLabels,
     .section = "Links",
     .label = "When the chosen window is on another Space",
     .description = "For links opened in a window you chose in the picker."},
    // Per profile.
    {.key = "updates.channel",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "stable",
     .choices = kUpdateChannels,
     .choice_labels = kUpdateChannelLabels,
     .section = "Updates",
     .label = "Update channel",
     .description = "Beta also gets versions for testing, before they "
                    "become stable. Only releases update themselves."},
    {.key = "updates.mode",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "install",
     .choices = kUpdateModes,
     .choice_labels = kUpdateModeLabels,
     .section = "Updates",
     .label = "Updates",
     .description = "Updates are installed when Brouk restarts. Brouk → "
                    "Check for Updates… checks at any time."},
    {.key = "task_manager.refresh_interval",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kChoice,
     .default_value = "1s",
     .choices = kTaskRefresh,
     .choice_labels = kTaskRefreshLabels,
     .section = "Task manager",
     .label = "Update values",
     .description = "How often the task manager measures CPU, memory and "
                    "network use while it is open."},
    {.key = "task_manager.history_length",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kInteger,
     .default_value = "5",
     .min_value = 1,
     .max_value = 60,
     .section = "Task manager",
     .label = "Graph history (minutes)",
     .description = "How far back the graph goes. Kept only while the task "
                    "manager is open."},
    {.key = "task_manager.columns",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "task, profile, memory, cpu, status, pid",
     .max_length = 256,
     .advanced = true,
     .section = "Task manager",
     .label = "Columns",
     .description = "The columns shown, in order: task, profile, memory, "
                    "cpu, status, pid, gpu, js_memory, network, address, "
                    "type, shared, window. Changed by right-clicking the "
                    "table's header."},
    {.key = "task_manager.sort",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kString,
     .default_value = "memory, descending",
     .max_length = 64,
     .advanced = true,
     .section = "Task manager",
     .label = "Sort order",
     .description = "A column and \"ascending\" or \"descending\". Changed "
                    "by clicking the table's header."},
    {.key = "task_manager.graph",
     .scope = SettingScope::kGlobal,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .advanced = true,
     .section = "Task manager",
     .label = "Show the graph",
     .description = "Whether the graph pane below the table is open."},
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
    {.key = "adblock.enabled",
     .scope = SettingScope::kProfile,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Ad blocking",
     .label = "Block ads and trackers",
     .description = "For every site in this profile, except those listed "
                    "below."},
    {.key = "adblock.allowlist",
     .scope = SettingScope::kProfile,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .format = SettingDef::Format::kUrlPatterns,
     .max_length = 16384,
     .section = "Ad blocking",
     .label = "Sites where blocking is off",
     .description = "Comma-separated, e.g. intranet.example.com, "
                    "*.corp.example. The shield button adds sites here."},
    {.key = "adblock.categories",
     .scope = SettingScope::kProfile,
     .type = SettingDef::Type::kString,
     .default_value = "ads, privacy, malware, cookies, regional, custom",
     .max_length = 256,
     .section = "Ad blocking",
     .label = "Kinds of lists used",
     .description = "ads, privacy, malware, domains (DNS-style lists), "
                    "cookies (cookie banners), annoyances, regional, "
                    "custom."},
    {.key = "downloads.folder",
     .scope = SettingScope::kProfile,
     .type = SettingDef::Type::kString,
     .default_value = "",
     .format = SettingDef::Format::kFolder,
     .max_length = 1024,
     .section = "Downloads",
     .label = "Download folder for this profile",
     .description = "Empty: the download folder for all profiles."},
    {.key = "passwords.source",
     .scope = SettingScope::kProfile,
     .type = SettingDef::Type::kChoice,
     .default_value = "builtin",
     .choices = kPasswordSources,
     .choice_labels = kPasswordSourceLabels,
     .section = "Passwords",
     .label = "Passwords",
     .description = "Where this profile's passwords are saved and filled "
                    "from."},
    {.key = "passwords.offer_to_save",
     .scope = SettingScope::kProfile,
     .type = SettingDef::Type::kBool,
     .default_value = "true",
     .section = "Passwords",
     .label = "Offer to save passwords"},
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
