#ifndef BROUK_CORE_HISTORY_SQL_H_
#define BROUK_CORE_HISTORY_SQL_H_

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.h"

namespace brouk {

// One profile's history for the SQL views.
struct SqlHistorySource {
  std::string profile_name;
  std::filesystem::path database;  // history.db; skipped if missing.
};

struct SqlCell {
  enum class Type { kNull, kInteger, kReal, kText };
  Type type = Type::kNull;
  std::string text;  // The value as text ("" for null).
};

struct SqlResult {
  std::vector<std::string> columns = {};
  std::vector<std::vector<SqlCell>> rows = {};
  bool truncated = false;  // Stopped at the row limit.
};

struct SqlLimits {
  std::size_t max_rows = 1000;
  std::chrono::milliseconds max_time{2000};
  std::size_t max_cell_bytes = std::size_t{64} * 1024;
};

// Runs one read-only SELECT of the history page's SQL editor (013) over
// the views
//   pages(profile, host, url, title, visit_count, typed_count, last_visit)
//   visits(profile, host, url, title, visit_time, transition)
// covering every source (profile is the profile's name; times are local
// "YYYY-MM-DD HH:MM:SS" text). The query runs on its own in-memory
// connection: the histories are attached read-only, copied into the two
// tables and detached again, and an authorizer lets the query read only
// those two tables, so it can neither change anything nor see anything
// else. Anything but a single read-only statement is rejected; the
// query stops after `limits.max_time` or `limits.max_rows` rows. Errors are
// SQLite's own messages.
Result<SqlResult> RunHistorySql(std::span<const SqlHistorySource> sources,
                                std::string_view sql,
                                const SqlLimits& limits = {});

}  // namespace brouk

#endif  // BROUK_CORE_HISTORY_SQL_H_
