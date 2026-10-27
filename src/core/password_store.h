#ifndef BROUK_CORE_PASSWORD_STORE_H_
#define BROUK_CORE_PASSWORD_STORE_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/crypto.h"
#include "core/result.h"
#include "core/sqlite_db.h"

namespace brouk {

// One saved login of the built-in password manager (008).
struct PasswordEntry {
  std::int64_t id = 0;
  // Where it was saved, from WebOriginOf ("https://example.com").
  std::string origin;
  // Further origins where it is used ("An entry can list additional sites").
  std::vector<std::string> other_origins;
  std::string username;
  // Secrets: encrypted in the database, plain only in memory.
  std::string password;
  std::string note;
  std::string totp;  // An otpauth:// URI (TotpSecretToUri) or "".
  // Bitwarden's "master password re-prompt": never filled automatically,
  // and the user confirms who they are before each fill. Built-in entries
  // never have it.
  bool reprompt = false;
  // Seconds since 1970 (UTC).
  std::int64_t created = 0;
  std::int64_t last_used = 0;
  std::int64_t password_changed = 0;
  std::int64_t times_used = 0;
};

// The entries offered on a page: those saved for its exact origin (filled
// automatically when there is one), and those of other hosts of the same
// site or saved over http for the https page (offered, never filled
// automatically; as in Chrome).
struct PasswordMatches {
  std::vector<PasswordEntry> exact;
  std::vector<PasswordEntry> same_site;
};

// One profile's built-in passwords, in profiles/<id>/passwords.db. The
// password, note and TOTP secret of each entry are encrypted with the
// profile's key, which lives in the keychain (SecretStore); sites and user
// names are not, so lookups need no decryption. Not thread-safe: the browser
// uses it from one thread only.
class PasswordStore {
  struct Key {};

 public:
  PasswordStore(Key /*key*/,
                std::unique_ptr<SqliteDatabase> db,
                const SecretKey& key);
  // Fails if the file cannot be opened, comes from a newer version, or was
  // encrypted with another key (the keychain item was lost or replaced).
  static Result<std::unique_ptr<PasswordStore>> Open(
      const std::filesystem::path& path,
      const SecretKey& key);

  // Adds an entry (its id is ignored); returns the new id. `now` sets the
  // created and password-changed times.
  Result<std::int64_t> Add(const PasswordEntry& entry, std::int64_t now);
  // Replaces an entry's sites, user name and secrets. The password-changed
  // time moves to `now` if the password changed.
  std::optional<Error> Update(const PasswordEntry& entry, std::int64_t now);
  // Removes an entry and returns it, so it can be restored (undo).
  std::optional<PasswordEntry> Remove(std::int64_t id);
  // Puts a removed entry back with its id and times.
  std::optional<Error> Restore(const PasswordEntry& entry);
  // The entry was filled: counts the use.
  void MarkUsed(std::int64_t id, std::int64_t now);

  std::optional<PasswordEntry> Get(std::int64_t id);
  // Every entry, ordered by site and user name.
  std::vector<PasswordEntry> All();
  // The entries for a page's origin (WebOriginOf).
  PasswordMatches Find(std::string_view origin);

  // "Never for this site": origins for which saving is not offered.
  std::vector<std::string> NeverSaveOrigins();
  bool IsNeverSave(std::string_view origin);
  void AddNeverSave(std::string_view origin);
  void RemoveNeverSave(std::string_view origin);

 private:
  std::optional<Error> CreateSchema();
  // Reads entries matching a WHERE clause with an optional id parameter,
  // with their secrets still sealed (see Unseal).
  std::vector<PasswordEntry> Query(std::string_view where,
                                   std::optional<std::int64_t> id);
  std::string Seal(std::string_view value, std::string_view field) const;
  // Decrypts the secrets of an entry read by Query.
  void Unseal(PasswordEntry& entry) const;

  std::unique_ptr<SqliteDatabase> db_;
  SecretKey key_;
};

}  // namespace brouk

#endif  // BROUK_CORE_PASSWORD_STORE_H_
