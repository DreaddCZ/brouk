#ifndef BROUK_CORE_HISTORY_STORE_H_
#define BROUK_CORE_HISTORY_STORE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.h"
#include "core/sqlite_db.h"

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

  // Removes visits before `cutoff` and pages left without visits (the
  // retention limit). Returns the number of visits removed.
  int DeleteVisitsBefore(std::int64_t cutoff);

  // The most recently visited pages, newest first (History → Recently
  // Visited).
  std::vector<HistoryMatch> RecentPages(std::size_t limit);

  // For tests and the history page later.
  std::optional<HistoryMatch> FindPage(std::string_view url);

 private:
  std::optional<Error> CreateSchema();

  std::unique_ptr<SqliteDatabase> db_;
};

}  // namespace brouk

#endif  // BROUK_CORE_HISTORY_STORE_H_
