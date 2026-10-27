#ifndef BROUK_CORE_BITWARDEN_SOURCE_H_
#define BROUK_CORE_BITWARDEN_SOURCE_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/bitwarden_cli.h"
#include "core/bitwarden_vault.h"
#include "core/password_store.h"
#include "core/result.h"

namespace brouk {

// Where a new login is saved in Bitwarden (the save prompt's choice).
struct BitwardenSaveLocation {
  std::string label;  // "No folder", "Work", "Company / Shared".
  std::string folder_id;
  std::string organization_id;
  std::string collection_id;
};

// The Bitwarden password source (008): the user's vault through the
// Bitwarden CLI, shared by every profile set to Bitwarden (the CLI has one
// signed-in account). While unlocked, the vault's logins are kept in memory
// only; Brouk never writes Bitwarden data to disk. Not thread-safe and
// blocking (it runs `bw`): used on the password thread only.
class BitwardenSource {
 public:
  enum class State {
    kNoCli,      // `bw` not found.
    kLoggedOut,  // `bw` has no signed-in account.
    kLocked,
    kUnlocked,
  };
  struct Status {
    State state = State::kNoCli;
    std::string user_email;
    std::string user_id;
    std::string server_url;  // Empty: Bitwarden's US cloud.
    // Why the status is not known (`bw` failed), for the user.
    std::string problem;
  };

  struct Options {
    std::string cli_setting;  // "Bitwarden CLI": a path, or "" to search.
    std::string search_path;  // PATH, for the search.
    std::int64_t lock_seconds = std::int64_t{15} * 60;
  };

  // Called for the server before `bw` connects to it: the file of its
  // certificate authorities, or "" for none (see BitwardenCli).
  using Certificates =
      std::function<std::filesystem::path(const std::string& server_url)>;

  BitwardenSource(BitwardenCli::Runner run,
                  std::function<bool(const std::filesystem::path&)> exists,
                  Certificates certificates);
  ~BitwardenSource();
  BitwardenSource(const BitwardenSource&) = delete;
  BitwardenSource& operator=(const BitwardenSource&) = delete;

  void SetOptions(Options options);

  // Finds `bw` and asks it for its state (fast: no network).
  Status CheckStatus();
  bool unlocked() const { return !session_.empty(); }
  // The session key, for "Unlock with Touch ID" (kept in the keychain).
  const std::string& session() const { return session_; }

  // Unlocks with the master password, then reads the vault.
  std::optional<Error> Unlock(std::string_view master_password,
                              std::int64_t now);
  // Unlocks with a session key from the keychain (Touch ID). Fails, with
  // kBitwardenLockedError, if the key no longer works.
  std::optional<Error> UseSession(std::string session, std::int64_t now);
  // Forgets the session and the logins. `end_session` also ends the
  // session in `bw` (when no session key is kept for Touch ID).
  void Lock(bool end_session);
  // Unlocked but unused for `lock_seconds`: time to lock.
  bool IsIdle(std::int64_t now) const;

  // Pulls the latest vault from the server and reads it again.
  std::optional<Error> Sync(std::int64_t now);

  // The logins for a page; empty while locked.
  PasswordMatches Find(std::string_view page_url, std::int64_t now);
  // The folders and collections a new login can go to.
  Result<std::vector<BitwardenSaveLocation>> SaveLocations(std::int64_t now);
  // Saves a new login (its origin, user name, password, 2FA secret) to
  // `location`, then syncs. Returns its entry id.
  Result<std::int64_t> Add(const PasswordEntry& entry,
                           const BitwardenSaveLocation& location,
                           std::int64_t now);
  // Changes the user name, password and 2FA secret of an entry from Find,
  // then syncs.
  std::optional<Error> Update(const PasswordEntry& entry, std::int64_t now);

 private:
  // The CLI, found anew when the setting changed. Null if `bw` is missing.
  BitwardenCli* Cli();
  // Reads the vault's logins into memory.
  std::optional<Error> Load();
  // A command failed: a locked vault forgets the session.
  Error Failed(const Error& error);
  void PrepareCertificates(const std::string& server_url);

  BitwardenCli::Runner run_;
  std::function<bool(const std::filesystem::path&)> exists_;
  Certificates certificates_;
  Options options_;
  std::unique_ptr<BitwardenCli> cli_;
  std::string certificates_server_;  // The server they were prepared for.
  std::string session_;
  BitwardenVault vault_;
  std::int64_t last_use_ = 0;
};

// What the user does when `bw` is missing or signed out (008): the steps
// shown instead of the unlock dialog. `certificates` is the file Brouk made
// for a company server's certificates (for `bw login`), or empty.
std::string BitwardenSetupSteps(const BitwardenSource::Status& status,
                                const std::filesystem::path& certificates);

}  // namespace brouk

#endif  // BROUK_CORE_BITWARDEN_SOURCE_H_
