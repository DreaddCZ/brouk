#ifndef BROUK_CORE_ADBLOCK_LISTS_H_
#define BROUK_CORE_ADBLOCK_LISTS_H_

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace brouk {

// Filter list categories (007). Lists are global; each profile chooses the
// categories it uses.
enum class FilterCategory {
  kAds,
  kPrivacy,
  kMalware,
  kDomains,
  kCookies,
  kAnnoyances,
  kRegional,
  kCustom,
};

inline constexpr std::size_t kFilterCategoryCount = 8;
std::span<const FilterCategory> AllFilterCategories();
// "ads", "privacy", …: the ids in settings.
std::string_view FilterCategoryId(FilterCategory category);
std::optional<FilterCategory> FilterCategoryFromId(std::string_view id);
std::string_view FilterCategoryLabel(FilterCategory category);
// The categories a profile uses unless it chooses otherwise: all but the
// domain lists and other annoyances.
std::set<FilterCategory> DefaultFilterCategories();

// A list Brouk knows (the table in 007) or one the user added by URL.
struct FilterListDef {
  std::string id;  // Stable: names its file and its settings entry.
  std::string name;
  FilterCategory category = FilterCategory::kCustom;
  std::string url;
  bool hosts_format = false;  // Hosts file or plain domains.
  bool trusted = false;       // uBlock's own lists (trusted scriptlets).
  // On for new installs (the regional list: by the system language).
  bool default_on = false;
  // Where "Report a broken site" goes for this list; empty for none.
  std::string issues_url;
};

std::span<const FilterListDef> BuiltinFilterLists();
const FilterListDef* FindBuiltinFilterList(std::string_view id);

// The lists in use: the defaults (the regional list only for Czech and
// Slovak system languages), with the user's changes (setting
// adblock.lists_on / adblock.lists_off: comma-separated ids), plus the
// lists added by URL.
struct FilterListChoice {
  std::vector<std::string> turned_on;
  std::vector<std::string> turned_off;
  std::vector<FilterListDef> custom;
};
std::vector<FilterListDef> ActiveFilterLists(const FilterListChoice& choice,
                                             std::string_view language);

// Lists added by URL, as the setting adblock.custom_lists stores them:
// "category=url" entries separated by spaces or new lines. Entries that are
// not http(s) URLs or name no category are skipped.
std::vector<FilterListDef> ParseCustomFilterLists(std::string_view text);
std::string FormatCustomFilterLists(std::span<const FilterListDef> lists);
// The id of a list added by URL: "url-" and a hash of the URL.
std::string CustomFilterListId(std::string_view url);

// The list's "! Expires: 4 days" header (hours), read from the comment
// lines at its top; nullopt without one.
std::optional<int> ParseExpiresHours(std::string_view list_text);
// How often a list is downloaded (007): by its header, at most every
// 6 hours; every 4 days without one.
int UpdateIntervalHours(std::optional<int> expires_hours);
inline constexpr int kMinUpdateHours = 6;
inline constexpr int kDefaultUpdateHours = 96;

// Comma-separated ids ("ads, privacy") to a set, ignoring unknown ones.
std::set<FilterCategory> ParseFilterCategories(std::string_view text);
std::string FormatFilterCategories(const std::set<FilterCategory>& categories);

}  // namespace brouk

#endif  // BROUK_CORE_ADBLOCK_LISTS_H_
