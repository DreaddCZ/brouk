#ifndef BROUK_CORE_SQLITE_DB_H_
#define BROUK_CORE_SQLITE_DB_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.h"

struct sqlite3;
struct sqlite3_stmt;

namespace brouk {

class SqliteStatement;

// One SQLite connection, closed when destroyed. A small wrapper so the rest
// of the code never touches SQLite's C API or its manual cleanup. Not
// thread-safe: use one connection per thread (the history uses its own
// thread).
class SqliteDatabase {
  struct Key {};  // Only Open() creates connections.

 public:
  SqliteDatabase(Key /*key*/, sqlite3* db) : db_(db) {}
  // Opens or creates the database file. ":memory:" for an in-memory one.
  static Result<std::unique_ptr<SqliteDatabase>> Open(
      const std::filesystem::path& path);
  ~SqliteDatabase();
  SqliteDatabase(const SqliteDatabase&) = delete;
  SqliteDatabase& operator=(const SqliteDatabase&) = delete;

  // Runs SQL without results (several statements allowed).
  std::optional<Error> Execute(std::string_view sql);
  // nullopt with the error in `error` if the SQL is invalid.
  std::optional<SqliteStatement> Prepare(std::string_view sql, Error& error);

  std::int64_t last_insert_id() const;
  int changes() const;  // Rows changed by the last statement.

 private:
  Error LastError(std::string_view what) const;

  sqlite3* db_;
};

// A prepared statement, finalized when destroyed. Bind values (indices start
// at 1), then Step() through the rows.
class SqliteStatement {
 public:
  ~SqliteStatement();
  SqliteStatement(SqliteStatement&& other) noexcept;
  SqliteStatement& operator=(SqliteStatement&&) = delete;
  SqliteStatement(const SqliteStatement&) = delete;
  SqliteStatement& operator=(const SqliteStatement&) = delete;

  void Bind(int index, std::int64_t value);
  void Bind(int index, std::string_view value);
  void BindBlob(int index, std::span<const std::uint8_t> value);

  // True while there is a row to read; false when done or on an error
  // (then ok() is false).
  bool Step();
  bool ok() const { return ok_; }
  // Runs a statement without results; false on an error.
  bool Run();
  // For running it again with other values.
  void Reset();

  std::int64_t ColumnInt(int index) const;
  std::string ColumnText(int index) const;
  std::vector<std::uint8_t> ColumnBlob(int index) const;

 private:
  friend class SqliteDatabase;
  explicit SqliteStatement(sqlite3_stmt* statement) : statement_(statement) {}

  sqlite3_stmt* statement_;
  bool ok_ = true;
};

}  // namespace brouk

#endif  // BROUK_CORE_SQLITE_DB_H_
