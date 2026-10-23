#include "core/history_store.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/command_palette.h"

namespace brouk {

namespace {

// Version 1: urls and visits. Favicons and downloads (013, 014) come later
// as new versions.
constexpr std::int64_t kSchemaVersion = 1;

constexpr std::int64_t kSecondsPerDay = std::int64_t{24} * 60 * 60;

// Candidates read for each query; more than a 90-day history usually has.
constexpr int kMaxCandidates = 20000;

std::string Lower(std::string_view text) {
  std::string result(text);
  for (char& c : result) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return result;
}

bool StartsWithNoCase(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         Lower(text.substr(0, prefix.size())) == prefix;
}

std::string HostOf(std::string_view url) {
  std::size_t start = url.find("://");
  if (start == std::string_view::npos) {
    return {};
  }
  start += 3;
  std::size_t end = url.find_first_of("/?#", start);
  std::string_view authority = url.substr(start, end - start);
  std::size_t at = authority.rfind('@');
  if (at != std::string_view::npos) {
    authority.remove_prefix(at + 1);
  }
  std::size_t colon = authority.rfind(':');
  if (colon != std::string_view::npos &&
      authority.find(']') == authority.npos) {
    authority = authority.substr(0, colon);
  }
  return Lower(authority);
}

std::vector<std::string> Words(std::string_view folded) {
  std::vector<std::string> words;
  std::size_t start = 0;
  while (start < folded.size()) {
    std::size_t end = folded.find(' ', start);
    if (end == std::string_view::npos) {
      end = folded.size();
    }
    if (end > start) {
      words.emplace_back(folded.substr(start, end - start));
    }
    start = end + 1;
  }
  return words;
}

bool IsSeparator(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) == 0;
}

// Whether `word` occurs in `text` at a word start; or anywhere.
enum class Found { kNo, kInside, kWordStart, kStart };
Found FindWord(std::string_view text, std::string_view word) {
  Found best = Found::kNo;
  for (std::size_t pos = text.find(word); pos != std::string_view::npos;
       pos = text.find(word, pos + 1)) {
    if (pos == 0) {
      return Found::kStart;
    }
    best = std::max(
        best, IsSeparator(text[pos - 1]) ? Found::kWordStart : Found::kInside);
  }
  return best;
}

// How well one typed word matches; 0 if it does not.
int WordScore(std::string_view word,
              std::string_view folded_url,
              std::string_view folded_title) {
  switch (FindWord(folded_url, word)) {
    case Found::kStart:
      return 1000;
    case Found::kWordStart:
      return 700;
    case Found::kInside:
      return std::max(
          400, FindWord(folded_title, word) >= Found::kWordStart ? 600 : 0);
    case Found::kNo:
      break;
  }
  switch (FindWord(folded_title, word)) {
    case Found::kStart:
    case Found::kWordStart:
      return 600;
    case Found::kInside:
      return 300;
    case Found::kNo:
      break;
  }
  return 0;
}

int UsageScore(std::int64_t typed,
               std::int64_t visits,
               std::int64_t last_visit,
               std::int64_t now) {
  int score = static_cast<int>(std::min<std::int64_t>(typed * 40, 400) +
                               std::min<std::int64_t>(visits * 10, 200));
  std::int64_t age = now - last_visit;
  if (age < kSecondsPerDay) {
    score += 200;
  } else if (age < 7 * kSecondsPerDay) {
    score += 120;
  } else if (age < 30 * kSecondsPerDay) {
    score += 60;
  }
  return score;
}

// The rest of `url` after `typed`, if `typed` is how it starts (with or
// without "http(s)://" and "www.", case-insensitive up to the path).
std::string InlineCompletion(std::string_view url, std::string_view typed) {
  if (typed.empty() || typed.back() == ' ') {
    return {};
  }
  std::string lowered = Lower(typed);
  std::string matching = UrlForMatching(url);  // "github.com/a"
  // "www.github.com/a"
  std::string_view without_scheme = url;
  std::size_t scheme = without_scheme.find("://");
  if (scheme != std::string_view::npos) {
    without_scheme.remove_prefix(scheme + 3);
  }
  // The address as stored ("https://www.github.com/a") last.
  for (std::string_view candidate :
       {std::string_view(matching), without_scheme, url}) {
    if (candidate.size() > typed.size() &&
        StartsWithNoCase(candidate, lowered)) {
      return std::string(candidate.substr(typed.size()));
    }
  }
  return {};
}

}  // namespace

bool ShouldRecordInHistory(std::string_view url) {
  return StartsWithNoCase(url, "http://") ||
         StartsWithNoCase(url, "https://") || StartsWithNoCase(url, "file://");
}

std::string UrlForMatching(std::string_view url) {
  std::string_view rest = url;
  if (StartsWithNoCase(rest, "https://")) {
    rest.remove_prefix(8);
  } else if (StartsWithNoCase(rest, "http://")) {
    rest.remove_prefix(7);
  } else {
    return Lower(url);
  }
  if (StartsWithNoCase(rest, "www.")) {
    rest.remove_prefix(4);
  }
  std::size_t path = rest.find_first_of("/?#");
  return Lower(rest.substr(0, path)) +
         std::string(path == std::string_view::npos ? std::string_view()
                                                    : rest.substr(path));
}

HistoryStore::HistoryStore(Key /*key*/, std::unique_ptr<SqliteDatabase> db)
    : db_(std::move(db)) {}

Result<std::unique_ptr<HistoryStore>> HistoryStore::Open(
    const std::filesystem::path& path) {
  Result<std::unique_ptr<SqliteDatabase>> db = SqliteDatabase::Open(path);
  if (!db) {
    return db.error();
  }
  auto store = std::make_unique<HistoryStore>(Key{}, std::move(db).value());
  if (std::optional<Error> error = store->CreateSchema()) {
    return *error;
  }
  return store;
}

std::optional<Error> HistoryStore::CreateSchema() {
  // WAL: a crash loses at most the last visits, never the database (013).
  if (std::optional<Error> error =
          db_->Execute("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;")) {
    return error;
  }
  Error error;
  std::optional<SqliteStatement> version =
      db_->Prepare("PRAGMA user_version", error);
  if (!version || !version->Step()) {
    return error;
  }
  std::int64_t current = version->ColumnInt(0);
  if (current > kSchemaVersion) {
    return Error{"The history was written by a newer version of the app."};
  }
  if (current == kSchemaVersion) {
    return std::nullopt;
  }
  return db_->Execute(
      "BEGIN;"
      "CREATE TABLE IF NOT EXISTS urls("
      "  id INTEGER PRIMARY KEY,"
      "  url TEXT NOT NULL UNIQUE,"
      "  host TEXT NOT NULL,"
      "  title TEXT NOT NULL DEFAULT '',"
      "  visit_count INTEGER NOT NULL DEFAULT 0,"
      "  typed_count INTEGER NOT NULL DEFAULT 0,"
      "  last_visit INTEGER NOT NULL);"
      "CREATE INDEX IF NOT EXISTS urls_host ON urls(host);"
      "CREATE TABLE IF NOT EXISTS visits("
      "  id INTEGER PRIMARY KEY,"
      "  url_id INTEGER NOT NULL REFERENCES urls(id) ON DELETE CASCADE,"
      "  visit_time INTEGER NOT NULL,"
      "  transition INTEGER NOT NULL);"
      "CREATE INDEX IF NOT EXISTS visits_url ON visits(url_id);"
      "CREATE INDEX IF NOT EXISTS visits_time ON visits(visit_time);"
      "PRAGMA user_version=1;"
      "COMMIT;");
}

std::optional<Error> HistoryStore::RecordVisit(std::string_view url,
                                               std::string_view title,
                                               std::int64_t time,
                                               VisitTransition transition) {
  Error error;
  std::optional<SqliteStatement> upsert = db_->Prepare(
      "INSERT INTO urls(url, host, title, visit_count, typed_count, "
      "last_visit) VALUES(?1, ?2, ?3, 1, ?4, ?5) "
      "ON CONFLICT(url) DO UPDATE SET "
      "  visit_count = visit_count + 1,"
      "  typed_count = typed_count + excluded.typed_count,"
      "  last_visit = max(last_visit, excluded.last_visit),"
      "  title = CASE WHEN excluded.title <> '' THEN excluded.title "
      "  ELSE title END "
      "RETURNING id",
      error);
  if (!upsert) {
    return error;
  }
  upsert->Bind(1, url);
  upsert->Bind(2, HostOf(url));
  upsert->Bind(3, title);
  upsert->Bind(4, std::int64_t{transition == VisitTransition::kTyped ? 1 : 0});
  upsert->Bind(5, time);
  if (!upsert->Step()) {
    return Error{"Could not record the visit."};
  }
  std::int64_t url_id = upsert->ColumnInt(0);
  upsert->Run();
  std::optional<SqliteStatement> visit = db_->Prepare(
      "INSERT INTO visits(url_id, visit_time, transition) VALUES(?1, ?2, ?3)",
      error);
  if (!visit) {
    return error;
  }
  visit->Bind(1, url_id);
  visit->Bind(2, time);
  visit->Bind(3, static_cast<std::int64_t>(transition));
  if (!visit->Run()) {
    return Error{"Could not record the visit."};
  }
  return std::nullopt;
}

std::optional<Error> HistoryStore::SetTitle(std::string_view url,
                                            std::string_view title) {
  Error error;
  std::optional<SqliteStatement> update =
      db_->Prepare("UPDATE urls SET title = ?2 WHERE url = ?1", error);
  if (!update) {
    return error;
  }
  update->Bind(1, url);
  update->Bind(2, title);
  if (!update->Run()) {
    return Error{"Could not save the page title."};
  }
  return std::nullopt;
}

std::vector<HistoryMatch> HistoryStore::Suggest(std::string_view input,
                                                std::int64_t now,
                                                std::size_t limit) {
  std::vector<HistoryMatch> matches;
  std::vector<std::string> words = Words(FoldForSearch(input));
  if (words.empty() || limit == 0) {
    return matches;
  }
  // Addresses are compared without "http(s)://" and "www.", so typed words
  // are too.
  for (std::string& word : words) {
    word = UrlForMatching(word);
    if (word.starts_with("www.")) {
      word.erase(0, 4);
    }
    if (word.empty()) {
      word = "www";
    }
  }
  Error error;
  std::optional<SqliteStatement> query = db_->Prepare(
      "SELECT url, title, visit_count, typed_count, last_visit FROM urls "
      "ORDER BY typed_count DESC, visit_count DESC, last_visit DESC "
      "LIMIT ?1",
      error);
  if (!query) {
    return matches;
  }
  query->Bind(1, std::int64_t{kMaxCandidates});
  std::string_view typed = input;
  while (!typed.empty() && typed.front() == ' ') {
    typed.remove_prefix(1);
  }
  while (query->Step()) {
    HistoryMatch match{.url = query->ColumnText(0),
                       .title = query->ColumnText(1),
                       .visit_count = query->ColumnInt(2),
                       .typed_count = query->ColumnInt(3),
                       .last_visit = query->ColumnInt(4),
                       .inline_completion = {}};
    std::string folded_url = FoldForSearch(UrlForMatching(match.url));
    std::string folded_title = FoldForSearch(match.title);
    int score = 0;
    for (const std::string& word : words) {
      int word_score = WordScore(word, folded_url, folded_title);
      if (word_score == 0) {
        score = 0;
        break;
      }
      score += word_score;
    }
    if (score == 0) {
      continue;
    }
    score +=
        UsageScore(match.typed_count, match.visit_count, match.last_visit, now);
    if (match.typed_count > 0) {
      match.inline_completion = InlineCompletion(match.url, typed);
      if (!match.inline_completion.empty()) {
        score += 500;  // The page the address bar completes comes first.
      }
    }
    match.score = score;
    matches.push_back(std::move(match));
  }
  std::ranges::stable_sort(matches,
                           [](const HistoryMatch& a, const HistoryMatch& b) {
                             if (a.score != b.score) {
                               return a.score > b.score;
                             }
                             return a.url.size() < b.url.size();
                           });
  if (matches.size() > limit) {
    matches.resize(limit);
  }
  return matches;
}

bool HistoryStore::DeletePage(std::string_view url) {
  Error error;
  std::optional<SqliteStatement> remove =
      db_->Prepare("DELETE FROM urls WHERE url = ?1", error);
  if (!remove) {
    return false;
  }
  remove->Bind(1, url);
  return remove->Run() && db_->changes() > 0;
}

void HistoryStore::RemoveLastVisit(std::string_view url) {
  Error error;
  std::optional<SqliteStatement> remove = db_->Prepare(
      "DELETE FROM visits WHERE id = (SELECT visits.id FROM visits "
      "JOIN urls ON urls.id = visits.url_id WHERE urls.url = ?1 "
      "ORDER BY visits.visit_time DESC, visits.id DESC LIMIT 1)",
      error);
  if (!remove) {
    return;
  }
  remove->Bind(1, url);
  if (!remove->Run()) {
    return;
  }
  std::optional<SqliteStatement> update = db_->Prepare(
      "UPDATE urls SET"
      "  visit_count = (SELECT count(*) FROM visits WHERE url_id = urls.id),"
      "  typed_count = (SELECT count(*) FROM visits"
      "    WHERE url_id = urls.id AND transition = 1),"
      "  last_visit = coalesce((SELECT max(visit_time) FROM visits"
      "    WHERE url_id = urls.id), last_visit) "
      "WHERE url = ?1",
      error);
  if (update) {
    update->Bind(1, url);
    update->Run();
  }
  std::optional<SqliteStatement> drop = db_->Prepare(
      "DELETE FROM urls WHERE url = ?1 AND visit_count = 0", error);
  if (drop) {
    drop->Bind(1, url);
    drop->Run();
  }
}

int HistoryStore::DeleteMatching(std::span<const UrlPattern> patterns) {
  if (patterns.empty()) {
    return 0;
  }
  Error error;
  std::optional<SqliteStatement> query =
      db_->Prepare("SELECT id, url FROM urls", error);
  std::optional<SqliteStatement> remove =
      db_->Prepare("DELETE FROM urls WHERE id = ?1", error);
  if (!query || !remove) {
    return 0;
  }
  std::vector<std::int64_t> ids;
  while (query->Step()) {
    if (MatchesAnyPattern(patterns, query->ColumnText(1))) {
      ids.push_back(query->ColumnInt(0));
    }
  }
  for (std::int64_t id : ids) {
    remove->Reset();
    remove->Bind(1, id);
    remove->Run();
  }
  return static_cast<int>(ids.size());
}

int HistoryStore::DeleteVisitsBefore(std::int64_t cutoff) {
  Error error;
  std::optional<SqliteStatement> remove =
      db_->Prepare("DELETE FROM visits WHERE visit_time < ?1", error);
  if (!remove) {
    return 0;
  }
  remove->Bind(1, cutoff);
  if (!remove->Run()) {
    return 0;
  }
  int removed = db_->changes();
  if (removed > 0) {
    db_->Execute(
        "BEGIN;"
        "DELETE FROM urls WHERE id NOT IN (SELECT url_id FROM visits);"
        "UPDATE urls SET"
        "  visit_count = (SELECT count(*) FROM visits WHERE url_id = urls.id),"
        "  typed_count = (SELECT count(*) FROM visits"
        "    WHERE url_id = urls.id AND transition = 1),"
        "  last_visit = (SELECT max(visit_time) FROM visits"
        "    WHERE url_id = urls.id);"
        "COMMIT;");
  }
  return removed;
}

std::vector<HistoryMatch> HistoryStore::RecentPages(std::size_t limit) {
  std::vector<HistoryMatch> pages;
  Error error;
  std::optional<SqliteStatement> query = db_->Prepare(
      "SELECT url, title, visit_count, typed_count, last_visit FROM urls "
      "ORDER BY last_visit DESC LIMIT ?1",
      error);
  if (!query) {
    return pages;
  }
  query->Bind(1, static_cast<std::int64_t>(limit));
  while (query->Step()) {
    pages.push_back({.url = query->ColumnText(0),
                     .title = query->ColumnText(1),
                     .visit_count = query->ColumnInt(2),
                     .typed_count = query->ColumnInt(3),
                     .last_visit = query->ColumnInt(4),
                     .inline_completion = {}});
  }
  return pages;
}

std::vector<HistorySite> HistoryStore::Sites() {
  std::vector<HistorySite> sites;
  Error error;
  std::optional<SqliteStatement> query = db_->Prepare(
      "SELECT host, sum(visit_count), count(*), max(last_visit) FROM urls "
      "GROUP BY host ORDER BY max(last_visit) DESC",
      error);
  if (!query) {
    return sites;
  }
  while (query->Step()) {
    sites.push_back({.host = query->ColumnText(0),
                     .visit_count = query->ColumnInt(1),
                     .page_count = query->ColumnInt(2),
                     .last_visit = query->ColumnInt(3)});
  }
  return sites;
}

std::vector<HistoryMatch> HistoryStore::PagesOfHost(std::string_view host) {
  std::vector<HistoryMatch> pages;
  Error error;
  std::optional<SqliteStatement> query = db_->Prepare(
      "SELECT url, title, visit_count, typed_count, last_visit FROM urls "
      "WHERE host = ?1 ORDER BY last_visit DESC",
      error);
  if (!query) {
    return pages;
  }
  query->Bind(1, host);
  while (query->Step()) {
    pages.push_back({.url = query->ColumnText(0),
                     .title = query->ColumnText(1),
                     .visit_count = query->ColumnInt(2),
                     .typed_count = query->ColumnInt(3),
                     .last_visit = query->ColumnInt(4),
                     .inline_completion = {}});
  }
  return pages;
}

std::vector<HistoryVisit> HistoryStore::Visits(std::int64_t before,
                                               std::size_t limit) {
  std::vector<HistoryVisit> visits;
  Error error;
  std::optional<SqliteStatement> query = db_->Prepare(
      "SELECT visits.id, urls.url, urls.title, urls.host, visits.visit_time, "
      "visits.transition FROM visits JOIN urls ON urls.id = visits.url_id "
      "WHERE ?1 = 0 OR visits.visit_time < ?1 "
      "ORDER BY visits.visit_time DESC, visits.id DESC LIMIT ?2",
      error);
  if (!query) {
    return visits;
  }
  query->Bind(1, before);
  query->Bind(2, static_cast<std::int64_t>(limit));
  while (query->Step()) {
    std::int64_t transition = query->ColumnInt(5);
    visits.push_back(
        {.id = query->ColumnInt(0),
         .url = query->ColumnText(1),
         .title = query->ColumnText(2),
         .host = query->ColumnText(3),
         .time = query->ColumnInt(4),
         .transition = transition >= 0 && transition <= 4
                           ? static_cast<VisitTransition>(transition)
                           : VisitTransition::kOther});
  }
  return visits;
}

int HistoryStore::DeleteHost(std::string_view host) {
  Error error;
  std::optional<SqliteStatement> remove =
      db_->Prepare("DELETE FROM urls WHERE host = ?1", error);
  if (!remove) {
    return 0;
  }
  remove->Bind(1, host);
  return remove->Run() ? db_->changes() : 0;
}

int HistoryStore::DeleteVisits(std::span<const std::int64_t> ids) {
  Error error;
  std::optional<SqliteStatement> remove =
      db_->Prepare("DELETE FROM visits WHERE id = ?1", error);
  if (!remove) {
    return 0;
  }
  int removed = 0;
  for (std::int64_t id : ids) {
    remove->Reset();
    remove->Bind(1, id);
    if (remove->Run()) {
      removed += db_->changes();
    }
  }
  if (removed > 0) {
    db_->Execute(
        "BEGIN;"
        "DELETE FROM urls WHERE id NOT IN (SELECT url_id FROM visits);"
        "UPDATE urls SET"
        "  visit_count = (SELECT count(*) FROM visits WHERE url_id = urls.id),"
        "  typed_count = (SELECT count(*) FROM visits"
        "    WHERE url_id = urls.id AND transition = 1),"
        "  last_visit = (SELECT max(visit_time) FROM visits"
        "    WHERE url_id = urls.id);"
        "COMMIT;");
  }
  return removed;
}

std::optional<HistoryMatch> HistoryStore::FindPage(std::string_view url) {
  Error error;
  std::optional<SqliteStatement> query = db_->Prepare(
      "SELECT url, title, visit_count, typed_count, last_visit FROM urls "
      "WHERE url = ?1",
      error);
  if (!query) {
    return std::nullopt;
  }
  query->Bind(1, url);
  if (!query->Step()) {
    return std::nullopt;
  }
  return HistoryMatch{.url = query->ColumnText(0),
                      .title = query->ColumnText(1),
                      .visit_count = query->ColumnInt(2),
                      .typed_count = query->ColumnInt(3),
                      .last_visit = query->ColumnInt(4),
                      .inline_completion = {}};
}

}  // namespace brouk
