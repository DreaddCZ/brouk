#include "core/history_query.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/command_palette.h"

namespace brouk {

namespace {

constexpr std::array<std::string_view, 10> kFields = {
    "site",   "host", "title",  "url",     "after",
    "before", "on",   "visits", "profile", "type"};

struct Token {
  std::size_t start = 0;
  std::size_t end = 0;
  bool negate = false;
  bool quoted = false;
  std::string field;  // "" for plain text.
  std::string value;
};

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n';
}

std::string Lower(std::string_view text) {
  std::string result(text);
  for (char& c : result) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return result;
}

bool IsField(std::string_view name) {
  return std::ranges::find(kFields, name) != kFields.end();
}

std::vector<Token> Tokenize(std::string_view text) {
  std::vector<Token> tokens;
  std::size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && IsSpace(text[i])) {
      ++i;
    }
    if (i >= text.size()) {
      break;
    }
    Token token;
    token.start = i;
    if (text[i] == '-' && i + 1 < text.size() && !IsSpace(text[i + 1])) {
      token.negate = true;
      ++i;
    }
    // field:value, where the field is one we know ("https://…" is text).
    std::size_t word_end = i;
    while (word_end < text.size() && !IsSpace(text[word_end]) &&
           text[word_end] != '"' && text[word_end] != ':') {
      ++word_end;
    }
    if (word_end < text.size() && text[word_end] == ':' &&
        IsField(Lower(text.substr(i, word_end - i)))) {
      token.field = Lower(text.substr(i, word_end - i));
      i = word_end + 1;
    }
    if (i < text.size() && text[i] == '"') {
      token.quoted = true;
      std::size_t close = text.find('"', i + 1);
      std::size_t value_end =
          close == std::string_view::npos ? text.size() : close;
      token.value = std::string(text.substr(i + 1, value_end - i - 1));
      i = close == std::string_view::npos ? text.size() : close + 1;
    } else {
      std::size_t value_start = i;
      while (i < text.size() && !IsSpace(text[i])) {
        ++i;
      }
      token.value = std::string(text.substr(value_start, i - value_start));
    }
    token.end = i;
    tokens.push_back(std::move(token));
  }
  return tokens;
}

std::optional<std::int64_t> ParseNumber(std::string_view text) {
  std::int64_t value = 0;
  auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size() || value < 0) {
    return std::nullopt;
  }
  return value;
}

std::tm LocalTime(std::int64_t time) {
  auto t = static_cast<std::time_t>(time);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &t);
#else
  localtime_r(&t, &local);
#endif
  return local;
}

std::int64_t FromLocal(std::tm local) {
  local.tm_isdst = -1;  // Let mktime decide (daylight saving time).
  return static_cast<std::int64_t>(std::mktime(&local));
}

// A day as the start of that local day: 2026-10-01, today, yesterday.
std::optional<std::int64_t> ParseDay(std::string_view text, std::int64_t now) {
  std::string lowered = Lower(text);
  if (lowered == "today" || lowered == "yesterday") {
    std::tm local = LocalTime(now);
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    if (lowered == "yesterday") {
      --local.tm_mday;
    }
    return FromLocal(local);
  }
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
    return std::nullopt;
  }
  std::optional<std::int64_t> year = ParseNumber(text.substr(0, 4));
  std::optional<std::int64_t> month = ParseNumber(text.substr(5, 2));
  std::optional<std::int64_t> day = ParseNumber(text.substr(8, 2));
  if (!year || !month || !day || *month < 1 || *month > 12 || *day < 1 ||
      *day > 31 || *year < 1970 || *year > 9999) {
    return std::nullopt;
  }
  std::tm local{};
  local.tm_year = static_cast<int>(*year - 1900);
  local.tm_mon = static_cast<int>(*month - 1);
  local.tm_mday = static_cast<int>(*day);
  return FromLocal(local);
}

// A time `text` ago: 7d, 2w, 3m (months), 1y.
std::optional<std::int64_t> ParseAgo(std::string_view text, std::int64_t now) {
  if (text.size() < 2) {
    return std::nullopt;
  }
  char unit =
      static_cast<char>(std::tolower(static_cast<unsigned char>(text.back())));
  std::optional<std::int64_t> count =
      ParseNumber(text.substr(0, text.size() - 1));
  if (!count || *count > 100000) {
    return std::nullopt;
  }
  std::tm local = LocalTime(now);
  auto n = static_cast<int>(*count);
  switch (unit) {
    case 'd':
      local.tm_mday -= n;
      break;
    case 'w':
      local.tm_mday -= 7 * n;
      break;
    case 'm':
      local.tm_mon -= n;
      break;
    case 'y':
      local.tm_year -= n;
      break;
    default:
      return std::nullopt;
  }
  return FromLocal(local);
}

std::optional<VisitTransition> ParseType(std::string_view text) {
  std::string lowered = Lower(text);
  if (lowered == "typed") {
    return VisitTransition::kTyped;
  }
  if (lowered == "link") {
    return VisitTransition::kLink;
  }
  if (lowered == "bookmark") {
    return VisitTransition::kBookmark;
  }
  if (lowered == "reload") {
    return VisitTransition::kReload;
  }
  if (lowered == "other") {
    return VisitTransition::kOther;
  }
  return std::nullopt;
}

bool HostMatchesSite(std::string_view host, std::string_view site) {
  return host == site || (host.size() > site.size() && host.ends_with(site) &&
                          host[host.size() - site.size() - 1] == '.');
}

}  // namespace

std::int64_t StartOfLocalDay(std::int64_t time) {
  std::tm local = LocalTime(time);
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_sec = 0;
  return FromLocal(local);
}

HistoryQuery HistoryQuery::Parse(std::string_view text, std::int64_t now) {
  HistoryQuery query;
  bool join_next = false;  // The previous token was OR.
  for (const Token& token : Tokenize(text)) {
    if (!token.negate && !token.quoted && token.field.empty() &&
        token.value == "OR") {
      if (query.clauses_.empty() || join_next) {
        query.problems_.push_back(
            {token.start, token.end, "OR needs a term on both sides"});
      }
      join_next = !query.clauses_.empty();
      continue;
    }
    Term term;
    term.negate = token.negate;
    auto problem = [&](std::string message) {
      query.problems_.push_back({token.start, token.end, std::move(message)});
    };
    const std::string& field = token.field;
    const std::string& value = token.value;
    if (field.empty()) {
      term.kind = Term::Kind::kText;
      term.text = FoldForSearch(value);
      if (term.text.empty()) {
        continue;
      }
    } else if (field == "site" || field == "host") {
      term.kind = field == "site" ? Term::Kind::kSite : Term::Kind::kHost;
      term.text = Lower(value);
      if (field == "site" && term.text.starts_with("*.")) {
        term.text.erase(0, 2);
      }
      if (term.text.empty()) {
        std::string message = field;
        message += ": needs a site, e.g. ";
        message += field;
        message += ":example.com";
        problem(std::move(message));
        continue;
      }
    } else if (field == "title" || field == "url" || field == "profile") {
      term.kind = field == "title" ? Term::Kind::kTitle
                  : field == "url" ? Term::Kind::kUrl
                                   : Term::Kind::kProfile;
      term.text = FoldForSearch(value);
      if (term.text.empty()) {
        problem(field + ": needs a word");
        continue;
      }
    } else if (field == "after" || field == "before") {
      term.kind = field == "after" ? Term::Kind::kAfter : Term::Kind::kBefore;
      std::optional<std::int64_t> time = ParseDay(value, now);
      if (!time) {
        time = ParseAgo(value, now);
      }
      if (!time) {
        problem(field +
                ": not a date; use e.g. 2026-10-01, 7d, 2w, 3m or "
                "today");
        continue;
      }
      term.value = *time;
    } else if (field == "on") {
      term.kind = Term::Kind::kOn;
      std::optional<std::int64_t> day = ParseDay(value, now);
      if (!day) {
        problem("on: not a day; use e.g. 2026-10-01, today or yesterday");
        continue;
      }
      term.value = *day;
      std::tm next = LocalTime(*day);
      ++next.tm_mday;
      term.value2 = FromLocal(next);
    } else if (field == "visits") {
      term.kind = Term::Kind::kVisits;
      std::string_view number = value;
      if (number.starts_with(">=")) {
        term.op = 'g';
        number.remove_prefix(2);
      } else if (number.starts_with("<=")) {
        term.op = 'l';
        number.remove_prefix(2);
      } else if (!number.empty() &&
                 (number[0] == '>' || number[0] == '<' || number[0] == '=')) {
        term.op = number[0];
        number.remove_prefix(1);
      }
      std::optional<std::int64_t> count = ParseNumber(number);
      if (!count) {
        problem("visits: needs a number, e.g. visits:>3");
        continue;
      }
      term.value = *count;
    } else {  // type
      term.kind = Term::Kind::kType;
      std::optional<VisitTransition> type = ParseType(value);
      if (!type) {
        problem("type: use typed, link, bookmark, reload or other");
        continue;
      }
      term.value = static_cast<std::int64_t>(*type);
    }
    if (join_next) {
      query.clauses_.back().push_back(std::move(term));
    } else {
      query.clauses_.push_back({std::move(term)});
    }
    join_next = false;
  }
  if (join_next) {
    query.problems_.push_back(
        {text.size(), text.size(), "OR needs a term on both sides"});
  }
  return query;
}

bool HistoryQuery::TermMatches(const Term& term, const QueryItem& item) const {
  switch (term.kind) {
    case Term::Kind::kText: {
      return FoldForSearch(item.title).find(term.text) != std::string::npos ||
             FoldForSearch(item.url).find(term.text) != std::string::npos;
    }
    case Term::Kind::kSite:
      return HostMatchesSite(Lower(item.host), term.text);
    case Term::Kind::kHost:
      return Lower(item.host) == term.text;
    case Term::Kind::kTitle:
      return FoldForSearch(item.title).find(term.text) != std::string::npos;
    case Term::Kind::kUrl:
      return FoldForSearch(item.url).find(term.text) != std::string::npos;
    case Term::Kind::kAfter:
      return item.time >= term.value;
    case Term::Kind::kBefore:
      return item.time < term.value;
    case Term::Kind::kOn:
      return item.time >= term.value && item.time < term.value2;
    case Term::Kind::kVisits:
      switch (term.op) {
        case '>':
          return item.page_visits > term.value;
        case '<':
          return item.page_visits < term.value;
        case 'g':
          return item.page_visits >= term.value;
        case 'l':
          return item.page_visits <= term.value;
        default:
          return item.page_visits == term.value;
      }
    case Term::Kind::kProfile:
      return FoldForSearch(item.profile_name) == term.text;
    case Term::Kind::kType:
      return static_cast<std::int64_t>(item.transition) == term.value;
  }
  return false;
}

bool HistoryQuery::Matches(const QueryItem& item) const {
  for (const std::vector<Term>& clause : clauses_) {
    bool any = false;
    for (const Term& term : clause) {
      if (TermMatches(term, item) != term.negate) {
        any = true;
        break;
      }
    }
    if (!any) {
      return false;
    }
  }
  return true;
}

}  // namespace brouk
