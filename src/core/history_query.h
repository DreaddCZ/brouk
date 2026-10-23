#ifndef BROUK_CORE_HISTORY_QUERY_H_
#define BROUK_CORE_HISTORY_QUERY_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/history_store.h"

namespace brouk {

// Something the query did not understand, as a range of the query text
// (byte offsets) and a hint for the user. The rest of the query still
// applies.
struct QueryProblem {
  std::size_t start = 0;
  std::size_t end = 0;
  std::string message;
};

// One visit as the query sees it.
struct QueryItem {
  std::string_view url;
  std::string_view title;
  std::string_view host;
  std::int64_t time = 0;  // Seconds since 1970.
  VisitTransition transition = VisitTransition::kOther;
  std::int64_t page_visits = 0;  // Visits of the page (visits:>3).
  std::string_view profile_name;
};

// The history page's search (013): plain words match the title or the URL
// (all words, ignoring case and accents); "exact phrase"; -word and
// -site:x exclude; a OR b; and the filters site:, host:, title:, url:,
// after:, before:, on: (2026-10-01, 7d, 2w, 3m, 1y, today, yesterday),
// visits: (>3, <3, =3, >=3, <=3), profile: and type: (typed, link,
// bookmark, reload, other). OR binds closer than the implied AND:
// "a b OR c" is a and (b or c).
class HistoryQuery {
 public:
  // `now` is the time relative dates count from.
  static HistoryQuery Parse(std::string_view text, std::int64_t now);

  bool empty() const { return clauses_.empty(); }
  const std::vector<QueryProblem>& problems() const { return problems_; }
  bool Matches(const QueryItem& item) const;

 private:
  struct Term {
    enum class Kind {
      kText,
      kSite,
      kHost,
      kTitle,
      kUrl,
      kAfter,   // time >= value
      kBefore,  // time < value
      kOn,      // value <= time < value2
      kVisits,
      kProfile,
      kType,
    };
    Kind kind = Kind::kText;
    bool negate = false;
    std::string text;  // Folded (lowercase, no accents).
    std::int64_t value = 0;
    std::int64_t value2 = 0;
    char op = '=';  // kVisits: '<', '>', '=', 'l' (<=), 'g' (>=).
  };
  bool TermMatches(const Term& term, const QueryItem& item) const;

  // All clauses must match; a clause matches if one of its terms does.
  std::vector<std::vector<Term>> clauses_;
  std::vector<QueryProblem> problems_;
};

// The start of the local day containing `time` (seconds since 1970).
std::int64_t StartOfLocalDay(std::int64_t time);

}  // namespace brouk

#endif  // BROUK_CORE_HISTORY_QUERY_H_
