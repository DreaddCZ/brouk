#include "core/history_profile.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <map>
#include <string_view>
#include <system_error>
#include <utility>

namespace brouk {

namespace {

constexpr std::int64_t kSecondsPerDay = std::int64_t{24} * 60 * 60;
// Untrusted files: what one entry may hold.
constexpr std::size_t kMaxUrlBytes = std::size_t{8} * 1024;
constexpr std::size_t kMaxTitleBytes = std::size_t{2} * 1024;
constexpr std::size_t kMaxVisitsPerPage = 100000;
constexpr std::size_t kMaxProblems = 20;

constexpr std::array<std::pair<VisitTransition, std::string_view>, 5>
    kTransitions = {{
        {VisitTransition::kLink, "link"},
        {VisitTransition::kTyped, "typed"},
        {VisitTransition::kBookmark, "bookmark"},
        {VisitTransition::kReload, "reload"},
        {VisitTransition::kOther, "other"},
    }};

std::string_view TransitionName(VisitTransition transition) {
  for (const auto& [value, name] : kTransitions) {
    if (value == transition) {
      return name;
    }
  }
  return "other";
}

std::optional<VisitTransition> TransitionFromName(std::string_view name) {
  for (const auto& [value, known] : kTransitions) {
    if (known == name) {
      return value;
    }
  }
  return std::nullopt;
}

YamlNode Sequence() {
  YamlNode node;
  node.kind = YamlNode::Kind::kSequence;
  return node;
}

const std::string* ScalarOf(const YamlNode& map, std::string_view key) {
  const YamlNode* value =
      map.kind == YamlNode::Kind::kMap ? map.Find(key) : nullptr;
  return value && value->kind == YamlNode::Kind::kScalar ? &value->scalar
                                                         : nullptr;
}

std::optional<std::int64_t> NumberOf(const YamlNode& map,
                                     std::string_view key) {
  const std::string* text = ScalarOf(map, key);
  if (!text || text->empty()) {
    return std::nullopt;
  }
  std::int64_t number = 0;
  const char* end = text->data() + text->size();
  const auto [ptr, ec] = std::from_chars(text->data(), end, number);
  if (ec != std::errc() || ptr != end) {
    return std::nullopt;
  }
  return number;
}

// One page of the export, its visits newest first.
struct ExportedPage {
  std::string url;
  std::string title;
  std::vector<std::pair<std::int64_t, VisitTransition>> visits;
};

}  // namespace

YamlNode ExportProfileHistory(HistoryStore& store,
                              const HistoryExportLimit& limit,
                              std::int64_t now) {
  const bool by_days = limit.kind == HistoryExportLimit::Kind::kDays;
  const int count = std::clamp(
      limit.count, 1, by_days ? kHistoryExportMaxDays : kHistoryExportMaxPages);
  const std::int64_t since =
      by_days ? now - static_cast<std::int64_t>(count) * kSecondsPerDay : 0;
  std::vector<ExportedPage> pages;  // In the order first seen: newest first.
  std::map<std::string, std::size_t> index;
  store.ForEachVisitNewestFirst([&](const HistoryVisit& visit) {
    if (by_days && visit.time < since) {
      return false;  // Newest first: the rest is older.
    }
    auto found = index.find(visit.url);
    if (found == index.end()) {
      if (!by_days && pages.size() >= static_cast<std::size_t>(count)) {
        return true;  // Only the visits of pages already taken.
      }
      found = index.emplace(visit.url, pages.size()).first;
      pages.push_back({visit.url, visit.title, {}});
    }
    pages[found->second].visits.emplace_back(visit.time, visit.transition);
    return true;
  });
  YamlNode section = Sequence();
  for (const ExportedPage& page : pages) {
    YamlNode entry = YamlNode::Map();
    entry.GetOrAdd("url") = YamlNode::Scalar(page.url, false);
    entry.GetOrAdd("title") = YamlNode::Scalar(page.title, false);
    YamlNode visits = Sequence();
    for (const auto& [time, transition] : page.visits) {
      YamlNode visit = YamlNode::Map();
      visit.GetOrAdd("time") = YamlNode::Scalar(std::to_string(time), true);
      visit.GetOrAdd("how") =
          YamlNode::Scalar(std::string(TransitionName(transition)), true);
      visits.items.push_back(std::move(visit));
    }
    entry.GetOrAdd("visits") = std::move(visits);
    section.items.push_back(std::move(entry));
  }
  return section;
}

std::optional<std::int64_t> HistoryKeepSince(std::string_view keep,
                                             std::int64_t now) {
  int days = 0;
  const auto [ptr, ec] =
      std::from_chars(keep.data(), keep.data() + keep.size(), days);
  if (ec != std::errc() || ptr != keep.data() + keep.size() || days <= 0) {
    return std::nullopt;  // "forever".
  }
  return now - static_cast<std::int64_t>(days) * kSecondsPerDay;
}

ProfileHistoryImport ImportProfileHistory(
    HistoryStore& store,
    const YamlNode& node,
    std::optional<std::int64_t> keep_since) {
  ProfileHistoryImport result;
  int skipped = 0;
  auto problem = [&](std::string text) {
    ++skipped;
    if (result.problems.size() < kMaxProblems) {
      result.problems.push_back("History: " + std::move(text));
    }
  };
  if (node.kind != YamlNode::Kind::kSequence) {
    problem("the section is not a list of pages.");
    return result;
  }
  std::vector<HistoryVisit> visits;
  for (const YamlNode& entry : node.items) {
    const std::string* url = ScalarOf(entry, "url");
    if (!url || url->empty() || url->size() > kMaxUrlBytes) {
      problem("an entry without a valid address was skipped.");
      continue;
    }
    if (!ShouldRecordInHistory(*url)) {
      problem("\"" + url->substr(0, 80) + "\" is not kept in the history.");
      continue;
    }
    const std::string* title = ScalarOf(entry, "title");
    const std::string title_text =
        title ? title->substr(0, kMaxTitleBytes) : std::string();
    const YamlNode* page_visits =
        entry.kind == YamlNode::Kind::kMap ? entry.Find("visits") : nullptr;
    if (!page_visits || page_visits->kind != YamlNode::Kind::kSequence ||
        page_visits->items.size() > kMaxVisitsPerPage) {
      problem("\"" + url->substr(0, 80) + "\" has no valid visits.");
      continue;
    }
    std::size_t taken = 0;
    for (const YamlNode& visit : page_visits->items) {
      const std::optional<std::int64_t> time = NumberOf(visit, "time");
      const std::string* how = ScalarOf(visit, "how");
      const std::optional<VisitTransition> transition =
          how ? TransitionFromName(*how)
              : std::optional(VisitTransition::kOther);
      if (!time || *time <= 0 || !transition) {
        problem("an invalid visit of \"" + url->substr(0, 80) +
                "\" was skipped.");
        continue;
      }
      if (keep_since && *time < *keep_since) {
        continue;  // Older than the history keeps (not a problem).
      }
      visits.push_back({.id = 0,
                        .url = *url,
                        .title = title_text,
                        .host = {},
                        .time = *time,
                        .transition = *transition});
      ++taken;
    }
    if (taken > 0) {
      ++result.pages;
    }
  }
  if (std::optional<Error> error = store.RecordVisits(visits)) {
    result.pages = 0;
    problem("the visits could not be saved: " + error->message);
    return result;
  }
  result.visits = static_cast<int>(visits.size());
  const auto listed = static_cast<int>(result.problems.size());
  if (skipped > listed) {
    result.problems.push_back("History: " + std::to_string(skipped - listed) +
                              " more entries were skipped.");
  }
  return result;
}

}  // namespace brouk
