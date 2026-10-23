#include "core/sqlite_db.h"

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace brouk {

Result<std::unique_ptr<SqliteDatabase>> SqliteDatabase::Open(
    const std::filesystem::path& path) {
  sqlite3* db = nullptr;
  int status = sqlite3_open_v2(
      path.string().c_str(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
      nullptr);
  if (status != SQLITE_OK) {
    std::string message = db ? sqlite3_errmsg(db) : sqlite3_errstr(status);
    sqlite3_close(db);
    return Error{"Could not open " + path.string() + ": " + message};
  }
  // Waits a little instead of failing if another connection holds a lock.
  sqlite3_busy_timeout(db, 2000);
  return std::make_unique<SqliteDatabase>(Key{}, db);
}

SqliteDatabase::~SqliteDatabase() {
  sqlite3_close(db_);
}

std::optional<Error> SqliteDatabase::Execute(std::string_view sql) {
  std::string text(sql);
  char* message = nullptr;
  if (sqlite3_exec(db_, text.c_str(), nullptr, nullptr, &message) !=
      SQLITE_OK) {
    Error error{std::string("Database error: ") +
                (message ? message : "unknown")};
    sqlite3_free(message);
    return error;
  }
  return std::nullopt;
}

std::optional<SqliteStatement> SqliteDatabase::Prepare(std::string_view sql,
                                                       Error& error) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()),
                         &statement, nullptr) != SQLITE_OK) {
    error = LastError("prepare");
    return std::nullopt;
  }
  return SqliteStatement(statement);
}

std::int64_t SqliteDatabase::last_insert_id() const {
  return sqlite3_last_insert_rowid(db_);
}

int SqliteDatabase::changes() const {
  return sqlite3_changes(db_);
}

Error SqliteDatabase::LastError(std::string_view what) const {
  return Error{"Database error (" + std::string(what) +
               "): " + sqlite3_errmsg(db_)};
}

SqliteStatement::~SqliteStatement() {
  sqlite3_finalize(statement_);
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : statement_(std::exchange(other.statement_, nullptr)), ok_(other.ok_) {}

void SqliteStatement::Bind(int index, std::int64_t value) {
  sqlite3_bind_int64(statement_, index, value);
}

void SqliteStatement::Bind(int index, std::string_view value) {
  sqlite3_bind_text(statement_, index, value.data(),
                    static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

bool SqliteStatement::Step() {
  int status = sqlite3_step(statement_);
  if (status == SQLITE_ROW) {
    return true;
  }
  ok_ = status == SQLITE_DONE;
  return false;
}

bool SqliteStatement::Run() {
  while (Step()) {
  }
  return ok_;
}

void SqliteStatement::Reset() {
  sqlite3_reset(statement_);
  sqlite3_clear_bindings(statement_);
  ok_ = true;
}

std::int64_t SqliteStatement::ColumnInt(int index) const {
  return sqlite3_column_int64(statement_, index);
}

std::string SqliteStatement::ColumnText(int index) const {
  const unsigned char* text = sqlite3_column_text(statement_, index);
  int size = sqlite3_column_bytes(statement_, index);
  return text ? std::string(reinterpret_cast<const char*>(text),
                            static_cast<std::size_t>(size))
              : std::string();
}

}  // namespace brouk
