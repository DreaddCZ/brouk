#ifndef BROUK_CORE_HISTORY_STORE_H_
#define BROUK_CORE_HISTORY_STORE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.h"
#include "core/sqlite_db.h"
#include "core/url_pattern.h"

namespace brouk {

// How a visit started (013: typed, link, bookmark, reload…).
enum class VisitTransition : std::int64_t {
  kLink = 0,
  kTyped = 1,
  kBookmark = 2,
  kReload = 3,
  kOther = 4,
};

// Whether a top-level navigation is recorded (013): http, https and file
// pages only; never brouk://, data:, blob:, error pages or downloads (the
// caller does not report those).
bool ShouldRecordInHistory(std::string_view url);

// The URL without "http(s)://" and "www.", lowercased up to the path, as
// address bar matching compares it: "https://www.GitHub.com/a" ->
// "github.com/a".
std::string UrlForMatching(std::string_view url);

// One page in the address bar suggestions.
struct HistoryMatch {
  std::string url;
  std::string title;
  std::int64_t visit_count = 0;
  std::int64_t typed_count = 0;
  std::int64_t last_visit = 0;  // Seconds since 1970 (UTC).
  int score = 0;
  // Text to add after the typed text to complete this URL inline ("" if it
  // cannot be completed inline). Only pages typed before (013).
  std::string inline_completion;
};

// One row of the history page's "by site" view (013): an exact host.
struct HistorySite {
  std::string host;  // "" for local files.
  std::int64_t visit_count = 0;
  std::int64_t page_count = 0;
  std::int64_t last_visit = 0;
};

// One visit, for the "by day" view.
struct HistoryVisit {
  std::int64_t id = 0;
  std::string url;
  std::string title;
  std::string host;
  std::int64_t time = 0;
  VisitTransition transition = VisitTransition::kOther;
};

class HistoryQuery;

// One profile's history database (013): pages and their visits, in
// profiles/<id>/history.db. Not thread-safe: the browser uses it from one
// thread only.
class HistoryStore {
  struct Key {};

 public:
  HistoryStore(Key /*key*/, std::unique_ptr<SqliteDatabase> db);
  static Result<std::unique_ptr<HistoryStore>> Open(
      const std::filesystem::path& path);

  // A visit at `time` (seconds since 1970). The title may be empty and set
  // later with SetTitle (pages get their title after loading).
  std::optional<Error> RecordVisit(std::string_view url,
                                   std::string_view title,
                                   std::int64_t time,
                                   VisitTransition transition);
  std::optional<Error> SetTitle(std::string_view url, std::string_view title);

  // Pages matching what was typed in the address bar, best first: every
  // word must appear in the URL or the title (case and accents ignored).
  // Ranked like Chrome: how the text matches (start of the address first),
  // then typed visits, visit count and how recent the last visit is (`now`).
  std::vector<HistoryMatch> Suggest(std::string_view input,
                                    std::int64_t now,
                                    std::size_t limit);

  // Removes a page and its visits (⇧⌫ on a suggestion). Returns whether it
  // existed.
  bool DeletePage(std::string_view url);
  // Removes the latest visit of a page that forwarded to another page by
  // itself (a client redirect), and the page if it has no other visits.
  void RemoveLastVisit(std::string_view url);
  // Removes every page matching one of `patterns` (never record these
  // sites). Returns the number of pages removed.
  int DeleteMatching(std::span<const UrlPattern> patterns);

  // Removes visits before `cutoff` and pages left without visits (the
  // retention limit). Returns the number of visits removed.
  int DeleteVisitsBefore(std::int64_t cutoff);

  // The most recently visited pages, newest first (History → Recently
  // Visited).
  std::vector<HistoryMatch> RecentPages(std::size_t limit);

  // The history page (013). Sites: every host, newest visit first. Pages of
  // one host, newest first. Visits before `before` (seconds; 0 for now),
  // newest first, at most `limit`.
  std::vector<HistorySite> Sites();
  std::vector<HistoryMatch> PagesOfHost(std::string_view host);
  std::vector<HistoryVisit> Visits(std::int64_t before, std::size_t limit);
  // The same, limited to what `query` matches (013 search). A visit is
  // tested as a whole (its page, time and type); a site or page counts only
  // its matching visits. `profile_name` is what profile: compares with.
  std::vector<HistorySite> SearchSites(const HistoryQuery& query,
                                       std::string_view profile_name);
  std::vector<HistoryMatch> SearchPagesOfHost(std::string_view host,
                                              const HistoryQuery& query,
                                              std::string_view profile_name);
  std::vector<HistoryVisit> SearchVisits(const HistoryQuery& query,
                                         std::string_view profile_name,
                                         std::int64_t before,
                                         std::size_t limit);

  // Removes all pages of a host. Returns the number removed.
  int DeleteHost(std::string_view host);
  // Removes single visits, and pages left without visits.
  int DeleteVisits(std::span<const std::int64_t> ids);

  // For tests and the history page later.
  std::optional<HistoryMatch> FindPage(std::string_view url);

 private:
  std::optional<Error> CreateSchema();
  // Calls `visit` for each visit (newest first) of `host` ("" for all
  // hosts) before `before` (0 for any), with its page's visit count, until
  // it returns false.
  void ForEachVisit(std::optional<std::string_view> host,
                    std::int64_t before,
                    const std::function<bool(const HistoryVisit&,
                                             std::int64_t page_visits)>& visit);

  std::unique_ptr<SqliteDatabase> db_;
};

}  // namespace brouk

#endif  // BROUK_CORE_HISTORY_STORE_H_
