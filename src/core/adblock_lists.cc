#include "core/adblock_lists.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace brouk {

namespace {

constexpr std::array<FilterCategory, kFilterCategoryCount> kCategories = {
    FilterCategory::kAds,      FilterCategory::kPrivacy,
    FilterCategory::kMalware,  FilterCategory::kDomains,
    FilterCategory::kCookies,  FilterCategory::kAnnoyances,
    FilterCategory::kRegional, FilterCategory::kCustom};

// uBlock Origin's report form; {url} is the page.
constexpr std::string_view kUblockIssues =
    "https://github.com/uBlockOrigin/uAssets/issues/new?template="
    "specific_report_from_ubo.yml&url_address_of_the_web_page={url}";
constexpr std::string_view kEasylistIssues =
    "https://github.com/easylist/easylist/issues/new";

const std::vector<FilterListDef>& Builtins() {
  static const std::vector<FilterListDef> lists = [] {
    using C = FilterCategory;
    auto def = [](std::string id, std::string name, C category, std::string url,
                  bool hosts, bool trusted, bool on, std::string_view issues) {
      return FilterListDef{.id = std::move(id),
                           .name = std::move(name),
                           .category = category,
                           .url = std::move(url),
                           .hosts_format = hosts,
                           .trusted = trusted,
                           .default_on = on,
                           .issues_url = std::string(issues)};
    };
    constexpr std::string_view kUassets =
        "https://ublockorigin.github.io/uAssets/filters/";
    std::string uassets(kUassets);
    return std::vector<FilterListDef>{
        def("easylist", "EasyList", C::kAds,
            "https://easylist.to/easylist/easylist.txt", false, false, true,
            kEasylistIssues),
        def("ublock-filters", "uBlock filters", C::kAds,
            uassets + "filters.txt", false, true, true, kUblockIssues),
        def("ublock-quick-fixes", "uBlock filters – Quick fixes", C::kAds,
            uassets + "quick-fixes.txt", false, true, true, kUblockIssues),
        def("ublock-unbreak", "uBlock filters – Unbreak", C::kAds,
            uassets + "unbreak.txt", false, true, true, kUblockIssues),
        def("easyprivacy", "EasyPrivacy", C::kPrivacy,
            "https://easylist.to/easylist/easyprivacy.txt", false, false, true,
            kEasylistIssues),
        def("ublock-privacy", "uBlock filters – Privacy", C::kPrivacy,
            uassets + "privacy.txt", false, true, true, kUblockIssues),
        def("url-tracking", "URL tracking parameters (AdGuard)", C::kPrivacy,
            "https://filters.adtidy.org/extension/ublock/filters/17.txt", false,
            false, true, ""),
        def("ublock-badware", "uBlock filters – Badware risks", C::kMalware,
            uassets + "badware.txt", false, true, true, kUblockIssues),
        def("urlhaus", "Malicious URL blocklist", C::kMalware,
            "https://malware-filter.gitlab.io/malware-filter/"
            "urlhaus-filter-ag-online.txt",
            false, false, true, ""),
        def("hagezi-multi", "Hagezi Multi Normal", C::kDomains,
            "https://raw.githubusercontent.com/hagezi/dns-blocklists/main/"
            "adblock/multi.txt",
            false, false, false, ""),
        def("oisd-small", "OISD small", C::kDomains, "https://small.oisd.nl/",
            false, false, false, ""),
        def("stevenblack", "StevenBlack hosts", C::kDomains,
            "https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts",
            true, false, false, ""),
        def("easylist-cookie", "EasyList Cookie", C::kCookies,
            "https://secure.fanboy.co.nz/fanboy-cookiemonster.txt", false,
            false, true, kEasylistIssues),
        def("ublock-cookies", "uBlock filters – Cookie notices", C::kCookies,
            uassets + "annoyances-cookies.txt", false, true, true,
            kUblockIssues),
        def("fanboy-annoyance", "Fanboy's Annoyance list", C::kAnnoyances,
            "https://secure.fanboy.co.nz/fanboy-annoyance.txt", false, false,
            false, kEasylistIssues),
        def("easylist-czsk", "EasyList Czech and Slovak", C::kRegional,
            "https://raw.githubusercontent.com/tomasko126/"
            "easylistczechandslovak/master/filters.txt",
            false, false, false, ""),
    };
  }();
  return lists;
}

std::string_view Trim(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front())) != 0) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back())) != 0) {
    text.remove_suffix(1);
  }
  return text;
}

std::string Lower(std::string_view text) {
  std::string lower(text);
  for (char& c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return lower;
}

std::vector<std::string_view> Split(std::string_view text,
                                    std::string_view separators) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = text.find_first_of(separators, start);
    std::string_view part = Trim(text.substr(
        start,
        end == std::string_view::npos ? std::string_view::npos : end - start));
    if (!part.empty()) {
      parts.push_back(part);
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return parts;
}

bool Contains(const std::vector<std::string>& ids, std::string_view id) {
  return std::ranges::find(ids, id) != ids.end();
}

}  // namespace

std::span<const FilterCategory> AllFilterCategories() {
  return kCategories;
}

std::string_view FilterCategoryId(FilterCategory category) {
  switch (category) {
    case FilterCategory::kAds:
      return "ads";
    case FilterCategory::kPrivacy:
      return "privacy";
    case FilterCategory::kMalware:
      return "malware";
    case FilterCategory::kDomains:
      return "domains";
    case FilterCategory::kCookies:
      return "cookies";
    case FilterCategory::kAnnoyances:
      return "annoyances";
    case FilterCategory::kRegional:
      return "regional";
    case FilterCategory::kCustom:
      return "custom";
  }
  return "custom";
}

std::optional<FilterCategory> FilterCategoryFromId(std::string_view id) {
  for (FilterCategory category : kCategories) {
    if (FilterCategoryId(category) == id) {
      return category;
    }
  }
  return std::nullopt;
}

std::string_view FilterCategoryLabel(FilterCategory category) {
  switch (category) {
    case FilterCategory::kAds:
      return "Ads";
    case FilterCategory::kPrivacy:
      return "Privacy";
    case FilterCategory::kMalware:
      return "Malware";
    case FilterCategory::kDomains:
      return "Domain lists (DNS-style)";
    case FilterCategory::kCookies:
      return "Cookie banners";
    case FilterCategory::kAnnoyances:
      return "Other annoyances";
    case FilterCategory::kRegional:
      return "Regional";
    case FilterCategory::kCustom:
      return "Custom";
  }
  return "Custom";
}

std::set<FilterCategory> DefaultFilterCategories() {
  std::set<FilterCategory> categories(kCategories.begin(), kCategories.end());
  categories.erase(FilterCategory::kDomains);
  categories.erase(FilterCategory::kAnnoyances);
  return categories;
}

std::span<const FilterListDef> BuiltinFilterLists() {
  return Builtins();
}

const FilterListDef* FindBuiltinFilterList(std::string_view id) {
  for (const FilterListDef& list : Builtins()) {
    if (list.id == id) {
      return &list;
    }
  }
  return nullptr;
}

std::vector<FilterListDef> ActiveFilterLists(const FilterListChoice& choice,
                                             std::string_view language) {
  std::string lang = Lower(language.substr(0, 2));
  std::vector<FilterListDef> active;
  for (const FilterListDef& list : Builtins()) {
    bool on = list.default_on ||
              (list.id == "easylist-czsk" && (lang == "cs" || lang == "sk"));
    if (Contains(choice.turned_on, list.id)) {
      on = true;
    }
    if (Contains(choice.turned_off, list.id)) {
      on = false;
    }
    if (on) {
      active.push_back(list);
    }
  }
  for (const FilterListDef& list : choice.custom) {
    if (!Contains(choice.turned_off, list.id)) {
      active.push_back(list);
    }
  }
  return active;
}

std::string CustomFilterListId(std::string_view url) {
  // FNV-1a: stable across runs and platforms (std::hash is not).
  std::uint64_t hash = 14695981039346656037ULL;
  for (char c : url) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 1099511628211ULL;
  }
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string id = "url-";
  for (int shift = 60; shift >= 0; shift -= 4) {
    id += kHex[(hash >> shift) & 0xF];
  }
  return id;
}

std::vector<FilterListDef> ParseCustomFilterLists(std::string_view text) {
  std::vector<FilterListDef> lists;
  for (std::string_view entry : Split(text, " \t\r\n")) {
    std::size_t equals = entry.find('=');
    if (equals == std::string_view::npos) {
      continue;
    }
    std::optional<FilterCategory> category =
        FilterCategoryFromId(entry.substr(0, equals));
    std::string_view url = entry.substr(equals + 1);
    std::string lower = Lower(url);
    if (!category ||
        !(lower.starts_with("https://") || lower.starts_with("http://"))) {
      continue;
    }
    std::string id = CustomFilterListId(url);
    bool seen = std::ranges::any_of(
        lists, [&](const FilterListDef& list) { return list.id == id; });
    if (seen) {
      continue;
    }
    lists.push_back({.id = id,
                     .name = std::string(url),
                     .category = *category,
                     .url = std::string(url),
                     .hosts_format = false,
                     .trusted = false,
                     .default_on = true,
                     .issues_url = ""});
  }
  return lists;
}

std::string FormatCustomFilterLists(std::span<const FilterListDef> lists) {
  std::string text;
  for (const FilterListDef& list : lists) {
    if (!text.empty()) {
      text += ' ';
    }
    text += std::string(FilterCategoryId(list.category)) + "=" + list.url;
  }
  return text;
}

std::optional<int> ParseExpiresHours(std::string_view list_text) {
  std::size_t pos = 0;
  // Headers are among the first comment lines.
  for (int lines = 0; lines < 50 && pos < list_text.size(); ++lines) {
    std::size_t end = list_text.find('\n', pos);
    std::string_view line = Trim(list_text.substr(
        pos,
        end == std::string_view::npos ? std::string_view::npos : end - pos));
    pos = end == std::string_view::npos ? list_text.size() : end + 1;
    if (line.empty() || line.starts_with('[')) {
      continue;
    }
    if (!line.starts_with('!') && !line.starts_with('#')) {
      break;
    }
    line = Trim(line.substr(1));
    std::string lower = Lower(line);
    if (!lower.starts_with("expires:")) {
      continue;
    }
    std::string_view value = Trim(std::string_view(lower).substr(8));
    int number = 0;
    auto [ptr, ec] =
        std::from_chars(value.data(), value.data() + value.size(), number);
    if (ec != std::errc() || number <= 0) {
      return std::nullopt;
    }
    std::string_view unit =
        Trim(value.substr(static_cast<std::size_t>(ptr - value.data())));
    if (unit.starts_with("hour") || unit.starts_with('h')) {
      return number;
    }
    // "days", "day" and no unit (days, as Adblock Plus reads it).
    return number * 24;
  }
  return std::nullopt;
}

int UpdateIntervalHours(std::optional<int> expires_hours) {
  if (!expires_hours) {
    return kDefaultUpdateHours;
  }
  return std::max(*expires_hours, kMinUpdateHours);
}

std::set<FilterCategory> ParseFilterCategories(std::string_view text) {
  std::set<FilterCategory> categories;
  for (std::string_view id : Split(text, ", ")) {
    if (std::optional<FilterCategory> category = FilterCategoryFromId(id)) {
      categories.insert(*category);
    }
  }
  return categories;
}

std::string FormatFilterCategories(const std::set<FilterCategory>& categories) {
  std::string text;
  for (FilterCategory category : kCategories) {
    if (categories.contains(category)) {
      if (!text.empty()) {
        text += ", ";
      }
      text += FilterCategoryId(category);
    }
  }
  return text;
}

}  // namespace brouk
