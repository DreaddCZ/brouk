#include "core/password_store.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "core/web_origin.h"

namespace brouk {

namespace {

constexpr std::int64_t kSchemaVersion = 1;
// A known value encrypted with the profile's key, to tell a lost or
// replaced keychain key apart from an empty store.
constexpr std::string_view kKeyCheck = "brouk-passwords";

constexpr std::string_view kColumns =
    "id, origin, other_origins, username, password, note, totp, created, "
    "last_used, password_changed, times_used";

std::string JoinLines(const std::vector<std::string>& lines) {
  std::string out;
  for (const std::string& line : lines) {
    if (!out.empty()) {
      out += '\n';
    }
    out += line;
  }
  return out;
}

std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> lines;
  while (!text.empty()) {
    std::size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    if (!line.empty()) {
      lines.emplace_back(line);
    }
    if (end == std::string_view::npos) {
      break;
    }
    text.remove_prefix(end + 1);
  }
  return lines;
}

}  // namespace

PasswordStore::PasswordStore(Key /*key*/,
                             std::unique_ptr<SqliteDatabase> db,
                             const SecretKey& key)
    : db_(std::move(db)), key_(key) {}

Result<std::unique_ptr<PasswordStore>> PasswordStore::Open(
    const std::filesystem::path& path,
    const SecretKey& key) {
  Result<std::unique_ptr<SqliteDatabase>> db = SqliteDatabase::Open(path);
  if (!db) {
    return db.error();
  }
  auto store =
      std::make_unique<PasswordStore>(Key{}, std::move(db).value(), key);
  if (std::optional<Error> error = store->CreateSchema()) {
    return *error;
  }
  return store;
}

std::optional<Error> PasswordStore::CreateSchema() {
  if (std::optional<Error> error =
          db_->Execute("PRAGMA journal_mode=WAL; PRAGMA secure_delete=ON;")) {
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
    return Error{"The passwords were saved by a newer version of the app."};
  }
  if (current < kSchemaVersion) {
    if (std::optional<Error> created = db_->Execute(
            "BEGIN;"
            "CREATE TABLE IF NOT EXISTS logins("
            "  id INTEGER PRIMARY KEY,"
            "  origin TEXT NOT NULL,"
            "  other_origins TEXT NOT NULL DEFAULT '',"
            "  username TEXT NOT NULL DEFAULT '',"
            "  password TEXT NOT NULL DEFAULT '',"
            "  note TEXT NOT NULL DEFAULT '',"
            "  totp TEXT NOT NULL DEFAULT '',"
            "  created INTEGER NOT NULL DEFAULT 0,"
            "  last_used INTEGER NOT NULL DEFAULT 0,"
            "  password_changed INTEGER NOT NULL DEFAULT 0,"
            "  times_used INTEGER NOT NULL DEFAULT 0);"
            "CREATE INDEX IF NOT EXISTS logins_origin ON logins(origin);"
            "CREATE TABLE IF NOT EXISTS never_save(origin TEXT PRIMARY KEY);"
            "CREATE TABLE IF NOT EXISTS meta(name TEXT PRIMARY KEY, value "
            "TEXT);"
            "PRAGMA user_version=1;"
            "COMMIT;")) {
      return created;
    }
  }
  std::optional<SqliteStatement> check =
      db_->Prepare("SELECT value FROM meta WHERE name = 'key_check'", error);
  if (!check) {
    return error;
  }
  if (check->Step()) {
    std::optional<std::string> sealed = Base64Decode(check->ColumnText(0));
    std::optional<std::string> plain =
        sealed ? Decrypt(key_, *sealed, "key_check") : std::nullopt;
    if (!plain || *plain != kKeyCheck) {
      return Error{
          "The saved passwords were encrypted with a key that is no longer "
          "in the keychain."};
    }
    return std::nullopt;
  }
  std::optional<SqliteStatement> insert = db_->Prepare(
      "INSERT INTO meta(name, value) VALUES('key_check', ?)", error);
  if (!insert) {
    return error;
  }
  insert->Bind(1, Base64Encode(Encrypt(key_, kKeyCheck, "key_check")));
  if (!insert->Run()) {
    return Error{"Could not set up the password storage."};
  }
  return std::nullopt;
}

std::string PasswordStore::Seal(std::string_view value,
                                std::string_view field) const {
  return value.empty() ? std::string()
                       : Base64Encode(Encrypt(key_, value, field));
}

void PasswordStore::Unseal(PasswordEntry& entry) const {
  for (auto [value, field] :
       {std::pair{&entry.password, "password"}, std::pair{&entry.note, "note"},
        std::pair{&entry.totp, "totp"}}) {
    if (value->empty()) {
      continue;
    }
    std::optional<std::string> sealed = Base64Decode(*value);
    std::optional<std::string> plain =
        sealed ? Decrypt(key_, *sealed, field) : std::nullopt;
    // A damaged value reads as empty rather than as ciphertext.
    *value = plain.value_or("");
  }
}

std::vector<PasswordEntry> PasswordStore::Query(
    std::string_view where,
    std::optional<std::int64_t> id) {
  std::vector<PasswordEntry> entries;
  Error error;
  std::optional<SqliteStatement> statement = db_->Prepare(
      "SELECT " + std::string(kColumns) + " FROM logins " + std::string(where),
      error);
  if (!statement) {
    return entries;
  }
  if (id) {
    statement->Bind(1, *id);
  }
  while (statement->Step()) {
    PasswordEntry entry;
    entry.id = statement->ColumnInt(0);
    entry.origin = statement->ColumnText(1);
    entry.other_origins = SplitLines(statement->ColumnText(2));
    entry.username = statement->ColumnText(3);
    entry.password = statement->ColumnText(4);
    entry.note = statement->ColumnText(5);
    entry.totp = statement->ColumnText(6);
    entry.created = statement->ColumnInt(7);
    entry.last_used = statement->ColumnInt(8);
    entry.password_changed = statement->ColumnInt(9);
    entry.times_used = statement->ColumnInt(10);
    entries.push_back(std::move(entry));
  }
  return entries;
}

Result<std::int64_t> PasswordStore::Add(const PasswordEntry& entry,
                                        std::int64_t now) {
  Error error;
  std::optional<SqliteStatement> statement = db_->Prepare(
      "INSERT INTO logins(origin, other_origins, username, password, note, "
      "totp, created, last_used, password_changed, times_used) "
      "VALUES(?, ?, ?, ?, ?, ?, ?, 0, ?, 0)",
      error);
  if (!statement) {
    return error;
  }
  statement->Bind(1, entry.origin);
  statement->Bind(2, JoinLines(entry.other_origins));
  statement->Bind(3, entry.username);
  statement->Bind(4, Seal(entry.password, "password"));
  statement->Bind(5, Seal(entry.note, "note"));
  statement->Bind(6, Seal(entry.totp, "totp"));
  statement->Bind(7, now);
  statement->Bind(8, now);
  if (!statement->Run()) {
    return Error{"Could not save the password."};
  }
  return db_->last_insert_id();
}

std::optional<Error> PasswordStore::Update(const PasswordEntry& entry,
                                           std::int64_t now) {
  std::optional<PasswordEntry> old = Get(entry.id);
  if (!old) {
    return Error{"The password no longer exists."};
  }
  const bool changed = !SecretsEqual(old->password, entry.password);
  Error error;
  std::optional<SqliteStatement> statement = db_->Prepare(
      "UPDATE logins SET origin = ?, other_origins = ?, username = ?, "
      "password = ?, note = ?, totp = ?, password_changed = ? WHERE id = ?",
      error);
  if (!statement) {
    return error;
  }
  statement->Bind(1, entry.origin);
  statement->Bind(2, JoinLines(entry.other_origins));
  statement->Bind(3, entry.username);
  statement->Bind(4, Seal(entry.password, "password"));
  statement->Bind(5, Seal(entry.note, "note"));
  statement->Bind(6, Seal(entry.totp, "totp"));
  statement->Bind(7, changed ? now : old->password_changed);
  statement->Bind(8, entry.id);
  if (!statement->Run()) {
    return Error{"Could not save the password."};
  }
  return std::nullopt;
}

std::optional<PasswordEntry> PasswordStore::Remove(std::int64_t id) {
  std::optional<PasswordEntry> entry = Get(id);
  if (!entry) {
    return std::nullopt;
  }
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("DELETE FROM logins WHERE id = ?", error);
  if (!statement) {
    return std::nullopt;
  }
  statement->Bind(1, id);
  if (!statement->Run()) {
    return std::nullopt;
  }
  return entry;
}

std::optional<Error> PasswordStore::Restore(const PasswordEntry& entry) {
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("INSERT OR REPLACE INTO logins(" + std::string(kColumns) +
                       ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                   error);
  if (!statement) {
    return error;
  }
  statement->Bind(1, entry.id);
  statement->Bind(2, entry.origin);
  statement->Bind(3, JoinLines(entry.other_origins));
  statement->Bind(4, entry.username);
  statement->Bind(5, Seal(entry.password, "password"));
  statement->Bind(6, Seal(entry.note, "note"));
  statement->Bind(7, Seal(entry.totp, "totp"));
  statement->Bind(8, entry.created);
  statement->Bind(9, entry.last_used);
  statement->Bind(10, entry.password_changed);
  statement->Bind(11, entry.times_used);
  if (!statement->Run()) {
    return Error{"Could not restore the password."};
  }
  return std::nullopt;
}

void PasswordStore::MarkUsed(std::int64_t id, std::int64_t now) {
  Error error;
  std::optional<SqliteStatement> statement = db_->Prepare(
      "UPDATE logins SET last_used = ?, times_used = times_used + 1 "
      "WHERE id = ?",
      error);
  if (statement) {
    statement->Bind(1, now);
    statement->Bind(2, id);
    statement->Run();
  }
}

std::optional<PasswordEntry> PasswordStore::Get(std::int64_t id) {
  std::vector<PasswordEntry> entries = Query("WHERE id = ?", id);
  if (entries.empty()) {
    return std::nullopt;
  }
  Unseal(entries.front());
  return std::move(entries.front());
}

std::vector<PasswordEntry> PasswordStore::All() {
  std::vector<PasswordEntry> entries = Query("", std::nullopt);
  for (PasswordEntry& entry : entries) {
    Unseal(entry);
  }
  std::ranges::sort(entries,
                    [](const PasswordEntry& a, const PasswordEntry& b) {
                      std::string site_a = SiteOfHost(HostOfOrigin(a.origin));
                      std::string site_b = SiteOfHost(HostOfOrigin(b.origin));
                      return std::tie(site_a, a.origin, a.username, a.id) <
                             std::tie(site_b, b.origin, b.username, b.id);
                    });
  return entries;
}

PasswordMatches PasswordStore::Find(std::string_view origin) {
  PasswordMatches matches;
  // Few hundred rows at most: compare the plain columns here and decrypt
  // only the matches.
  for (PasswordEntry& entry : Query("ORDER BY username, id", std::nullopt)) {
    bool exact = entry.origin == origin ||
                 std::ranges::find(entry.other_origins, origin) !=
                     entry.other_origins.end();
    bool same_site =
        !exact && (IsSameSiteOrUpgrade(entry.origin, origin) ||
                   std::ranges::any_of(
                       entry.other_origins, [&](const std::string& other) {
                         return IsSameSiteOrUpgrade(other, origin);
                       }));
    if (exact || same_site) {
      Unseal(entry);
      (exact ? matches.exact : matches.same_site).push_back(std::move(entry));
    }
  }
  return matches;
}

std::vector<std::string> PasswordStore::NeverSaveOrigins() {
  std::vector<std::string> origins;
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("SELECT origin FROM never_save ORDER BY origin", error);
  while (statement && statement->Step()) {
    origins.push_back(statement->ColumnText(0));
  }
  return origins;
}

bool PasswordStore::IsNeverSave(std::string_view origin) {
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("SELECT 1 FROM never_save WHERE origin = ?", error);
  if (!statement) {
    return false;
  }
  statement->Bind(1, origin);
  return statement->Step();
}

void PasswordStore::AddNeverSave(std::string_view origin) {
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("INSERT OR IGNORE INTO never_save(origin) VALUES(?)", error);
  if (statement) {
    statement->Bind(1, origin);
    statement->Run();
  }
}

void PasswordStore::RemoveNeverSave(std::string_view origin) {
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("DELETE FROM never_save WHERE origin = ?", error);
  if (statement) {
    statement->Bind(1, origin);
    statement->Run();
  }
}

}  // namespace brouk
