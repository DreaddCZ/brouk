#include "core/history_sql.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace brouk {

namespace {

struct CloseDatabase {
  void operator()(sqlite3* db) const { sqlite3_close(db); }
};
struct FinalizeStatement {
  void operator()(sqlite3_stmt* statement) const {
    sqlite3_finalize(statement);
  }
};
using Database = std::unique_ptr<sqlite3, CloseDatabase>;
using Statement = std::unique_ptr<sqlite3_stmt, FinalizeStatement>;

// A file: URI for ATTACH, read-only. Everything but plain path characters
// is percent-encoded.
std::string ReadOnlyUri(const std::filesystem::path& path) {
  std::string uri = "file:";
  for (unsigned char c : path.string()) {
    if (std::isalnum(c) != 0 || c == '/' || c == '.' || c == '-' || c == '_' ||
        c == '~') {
      uri += static_cast<char>(c);
    } else {
      constexpr std::string_view kHex = "0123456789ABCDEF";
      uri += '%';
      uri += kHex[c >> 4];
      uri += kHex[c & 0x0F];
    }
  }
  return uri + "?mode=ro";
}

std::string Quoted(std::string_view text) {
  std::string quoted = "'";
  for (char c : text) {
    quoted += c;
    if (c == '\'') {
      quoted += '\'';
    }
  }
  return quoted + "'";
}

Error LastError(sqlite3* db) {
  return Error{sqlite3_errmsg(db)};
}

// The query may read only the two tables pages and visits; nothing else is
// allowed: no writes, PRAGMA, ATTACH, or the schema tables. (Reads without a
// database are of the query's own common table expressions.)
int Authorize(void* /*data*/,
              int action,
              const char* table,
              const char* /*column*/,
              const char* database,
              const char* /*inner_view*/) {
  switch (action) {
    case SQLITE_SELECT:
    case SQLITE_RECURSIVE:
    case SQLITE_FUNCTION:
      return SQLITE_OK;
    case SQLITE_READ:
      if (database == nullptr) {
        return SQLITE_OK;
      }
      if (std::strcmp(database, "temp") == 0 && table != nullptr &&
          (std::strcmp(table, "pages") == 0 ||
           std::strcmp(table, "visits") == 0)) {
        return SQLITE_OK;
      }
      return SQLITE_DENY;
    default:
      return SQLITE_DENY;
  }
}

struct Deadline {
  std::chrono::steady_clock::time_point at;
};

int CheckDeadline(void* data) {
  const auto* deadline = static_cast<const Deadline*>(data);
  return std::chrono::steady_clock::now() > deadline->at ? 1 : 0;
}

bool HasTable(sqlite3* db, std::string_view schema, std::string_view table) {
  std::string sql = "SELECT 1 FROM " + std::string(schema) +
                    ".sqlite_master WHERE type = 'table' AND name = ?1";
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK) {
    return false;
  }
  Statement statement(raw);
  sqlite3_bind_text(raw, 1, table.data(), static_cast<int>(table.size()),
                    SQLITE_TRANSIENT);
  return sqlite3_step(raw) == SQLITE_ROW;
}

// The first keyword of the query, upper case (comments skipped).
std::string FirstKeyword(std::string_view sql) {
  std::size_t i = 0;
  while (i < sql.size()) {
    if (std::isspace(static_cast<unsigned char>(sql[i])) != 0) {
      ++i;
    } else if (sql.substr(i, 2) == "--") {
      std::size_t end = sql.find('\n', i);
      i = end == std::string_view::npos ? sql.size() : end;
    } else if (sql.substr(i, 2) == "/*") {
      std::size_t end = sql.find("*/", i + 2);
      i = end == std::string_view::npos ? sql.size() : end + 2;
    } else {
      break;
    }
  }
  std::string word;
  while (i < sql.size() && std::isalpha(static_cast<unsigned char>(sql[i]))) {
    word += static_cast<char>(std::toupper(static_cast<unsigned char>(sql[i])));
    ++i;
  }
  return word;
}

bool OnlyWhitespace(const char* text) {
  for (; text != nullptr && *text != '\0'; ++text) {
    if (std::isspace(static_cast<unsigned char>(*text)) == 0 && *text != ';') {
      return false;
    }
  }
  return true;
}

}  // namespace

Result<SqlResult> RunHistorySql(std::span<const SqlHistorySource> sources,
                                std::string_view sql,
                                const SqlLimits& limits) {
  std::string keyword = FirstKeyword(sql);
  if (keyword != "SELECT" && keyword != "WITH" && keyword != "VALUES") {
    return Error{"Only SELECT queries can be run."};
  }
  sqlite3* raw_db = nullptr;
  int status = sqlite3_open_v2(":memory:", &raw_db,
                               SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                   SQLITE_OPEN_URI | SQLITE_OPEN_NOMUTEX,
                               nullptr);
  Database db(raw_db);
  if (status != SQLITE_OK) {
    return Error{"Could not open the history for SQL."};
  }
  // The histories, read-only, and the views over them.
  std::string pages;
  std::string visits;
  int attached = 0;
  for (const SqlHistorySource& source : sources) {
    std::error_code error;
    if (!std::filesystem::exists(source.database, error)) {
      continue;
    }
    std::string schema = "p" + std::to_string(attached);
    sqlite3_stmt* raw = nullptr;
    std::string attach = "ATTACH ?1 AS " + schema;
    if (sqlite3_prepare_v2(db.get(), attach.c_str(), -1, &raw, nullptr) !=
        SQLITE_OK) {
      return LastError(db.get());
    }
    Statement statement(raw);
    std::string uri = ReadOnlyUri(source.database);
    sqlite3_bind_text(raw, 1, uri.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(raw) != SQLITE_DONE) {
      continue;  // Unreadable: left out.
    }
    ++attached;
    if (!HasTable(db.get(), schema, "urls") ||
        !HasTable(db.get(), schema, "visits")) {
      continue;
    }
    std::string name = Quoted(source.profile_name);
    pages += (pages.empty() ? "" : " UNION ALL ");
    pages += "SELECT ";
    pages += name;
    pages +=
        " AS profile, host, url, title, visit_count, typed_count, "
        "datetime(last_visit, 'unixepoch', 'localtime') AS last_visit FROM ";
    pages += schema;
    pages += ".urls";
    visits += (visits.empty() ? "" : " UNION ALL ");
    visits += "SELECT ";
    visits += name;
    visits +=
        " AS profile, u.host, u.url, u.title, "
        "datetime(v.visit_time, 'unixepoch', 'localtime') AS visit_time, "
        "CASE v.transition WHEN 0 THEN 'link' WHEN 1 THEN 'typed' "
        "WHEN 2 THEN 'bookmark' WHEN 3 THEN 'reload' ELSE 'other' END "
        "AS transition FROM ";
    visits += schema;
    visits += ".visits v JOIN ";
    visits += schema;
    visits += ".urls u ON u.id = v.url_id";
  }
  if (pages.empty()) {
    pages =
        "SELECT NULL AS profile, NULL AS host, NULL AS url, NULL AS title, "
        "NULL AS visit_count, NULL AS typed_count, NULL AS last_visit "
        "WHERE 0";
    visits =
        "SELECT NULL AS profile, NULL AS host, NULL AS url, NULL AS title, "
        "NULL AS visit_time, NULL AS transition WHERE 0";
  }
  // Copied into memory, then the histories are detached: the query sees
  // these two tables and nothing else.
  std::string tables = "CREATE TEMP TABLE pages AS " + pages +
                       "; CREATE TEMP TABLE visits AS " + visits + ";";
  for (int i = 0; i < attached; ++i) {
    tables += "DETACH p" + std::to_string(i) + ";";
  }
  if (sqlite3_exec(db.get(), tables.c_str(), nullptr, nullptr, nullptr) !=
      SQLITE_OK) {
    return LastError(db.get());
  }

  // From here on the query's rules apply.
  sqlite3_set_authorizer(db.get(), Authorize, nullptr);
  Deadline deadline{std::chrono::steady_clock::now() + limits.max_time};
  sqlite3_progress_handler(db.get(), 1000, CheckDeadline, &deadline);

  std::string text(sql);
  sqlite3_stmt* raw = nullptr;
  const char* tail = nullptr;
  if (sqlite3_prepare_v2(db.get(), text.c_str(), static_cast<int>(text.size()),
                         &raw, &tail) != SQLITE_OK) {
    return LastError(db.get());
  }
  Statement statement(raw);
  if (!raw) {
    return Error{"The query is empty."};
  }
  if (!OnlyWhitespace(tail)) {
    return Error{"Only one statement can be run at a time."};
  }
  if (sqlite3_stmt_readonly(raw) == 0) {
    return Error{"Only SELECT queries can be run."};
  }
  SqlResult result;
  int columns = sqlite3_column_count(raw);
  for (int i = 0; i < columns; ++i) {
    const char* name = sqlite3_column_name(raw, i);
    result.columns.emplace_back(name ? name : "");
  }
  while (true) {
    int step = sqlite3_step(raw);
    if (step == SQLITE_DONE) {
      break;
    }
    if (step == SQLITE_INTERRUPT) {
      std::int64_t ms = limits.max_time.count();
      std::string limit = ms % 1000 == 0
                              ? std::to_string(ms / 1000) + " seconds"
                              : std::to_string(ms) + " ms";
      return Error{"The query took longer than " + limit + " and was stopped."};
    }
    if (step != SQLITE_ROW) {
      return LastError(db.get());
    }
    if (result.rows.size() >= limits.max_rows) {
      result.truncated = true;
      break;
    }
    std::vector<SqlCell> row;
    for (int i = 0; i < columns; ++i) {
      SqlCell cell;
      switch (sqlite3_column_type(raw, i)) {
        case SQLITE_NULL:
          cell.type = SqlCell::Type::kNull;
          break;
        case SQLITE_INTEGER:
          cell.type = SqlCell::Type::kInteger;
          break;
        case SQLITE_FLOAT:
          cell.type = SqlCell::Type::kReal;
          break;
        default:
          cell.type = SqlCell::Type::kText;
          break;
      }
      if (cell.type != SqlCell::Type::kNull) {
        const unsigned char* value = sqlite3_column_text(raw, i);
        int size = sqlite3_column_bytes(raw, i);
        if (value) {
          cell.text.assign(reinterpret_cast<const char*>(value),
                           std::min<std::size_t>(static_cast<std::size_t>(size),
                                                 limits.max_cell_bytes));
        }
      }
      row.push_back(std::move(cell));
    }
    result.rows.push_back(std::move(row));
  }
  return result;
}

}  // namespace brouk
