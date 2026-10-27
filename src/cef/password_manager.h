#ifndef BROUK_CEF_PASSWORD_MANAGER_H_
#define BROUK_CEF_PASSWORD_MANAGER_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/bitwarden_source.h"
#include "core/password_generator.h"
#include "core/password_store.h"
#include "core/profile_id.h"
#include "core/result.h"
#include "core/secret_store.h"
#include "include/cef_thread.h"

namespace brouk {

// Settings the password manager follows (008), from the global settings.
struct PasswordOptions {
  bool fill_on_click = false;
  bool copy_totp = false;
  bool breach_check = true;
  PasswordRules rules;
  // Bitwarden (008).
  std::string bitwarden_cli;  // The "Bitwarden CLI" setting.
  std::int64_t bitwarden_lock_minutes = 15;
  bool bitwarden_touch_id = true;
};

// What a page's origin has in a profile's passwords.
struct PasswordLookup {
  PasswordMatches matches;
  bool never_save = false;
  bool offer_to_save = true;
  // The passwords could not be opened (keychain locked or key lost): no
  // filling and no saving; the message is for the user.
  std::optional<std::string> error;
  // A Bitwarden profile whose vault is locked: nothing is found until the
  // user unlocks it (the key icon shows a lock).
  bool locked = false;
};

// What unlocking Bitwarden starts from (008).
struct BitwardenUnlockState {
  BitwardenSource::Status status;
  // A session key is kept for "Unlock with Touch ID".
  bool stored_session = false;
  // For a missing or signed-out `bw`: what the user does.
  std::string setup_steps;
};

// The profiles' built-in passwords (008), used from the UI thread. Every
// database and keychain access runs on one dedicated thread, so the UI never
// waits for the disk or the keychain; results come back on the UI thread.
// Passwords are never logged.
class PasswordManager {
 public:
  // `database_path` gives a profile's passwords.db; called on the password
  // thread. `keychain` holds the profiles' keys and is used on that thread.
  // UI thread.
  PasswordManager(
      std::function<std::filesystem::path(const ProfileId&)> database_path,
      std::shared_ptr<SecretStore> keychain);
  // Waits for queued work and closes the databases. UI thread.
  ~PasswordManager();
  PasswordManager(const PasswordManager&) = delete;
  PasswordManager& operator=(const PasswordManager&) = delete;

  const PasswordOptions& options() const { return options_; }
  void SetOptions(const PasswordOptions& options);
  // Whether a profile offers to save passwords (its setting).
  void SetOfferToSave(std::function<bool(const ProfileId&)> offer_to_save) {
    offer_to_save_ = std::move(offer_to_save);
  }

  // Bitwarden (008): which profiles use it (UI thread), the certificates of
  // a server (called on the password thread) and the folders where `bw` is
  // looked for (PATH style).
  void SetBitwarden(std::function<bool(const ProfileId&)> uses_bitwarden,
                    BitwardenSource::Certificates certificates,
                    std::string search_path);
  bool UsesBitwarden(const ProfileId& profile) const;

  using LookupDone = std::function<void(PasswordLookup)>;
  // `url` is the page's full address (Bitwarden's match settings may look at
  // the path); `origin` is its WebOriginOf.
  void Lookup(const ProfileId& profile,
              std::string origin,
              std::string url,
              LookupDone done);

  using SaveDone = std::function<void(Result<std::int64_t>)>;
  // `location` is where a Bitwarden profile saves (default: no folder).
  void Add(const ProfileId& profile,
           PasswordEntry entry,
           SaveDone done,
           BitwardenSaveLocation location = {});
  using ErrorDone = std::function<void(std::optional<Error>)>;
  void Update(const ProfileId& profile, PasswordEntry entry, ErrorDone done);
  void MarkUsed(const ProfileId& profile, std::int64_t id);
  void SetNeverSave(const ProfileId& profile, std::string origin, bool never);

  // Runs `work` with `profile`'s passwords on the password thread (not if
  // they cannot be opened: then `failed` gets the reason), then `done` on
  // the UI thread. For the passwords page; `work` must not keep the store.
  void Run(const ProfileId& profile,
           std::function<void(PasswordStore&)> work,
           std::function<void(std::optional<std::string> failed)> done);

  // Closes a profile's database before its folder is deleted (005), and
  // removes its key from the keychain.
  void DeleteProfile(const ProfileId& profile);

  // Bitwarden, shared by every profile that uses it (008).
  //
  // Whether the vault is unlocked, as last known on the UI thread, and a
  // number that changes whenever it locks or unlocks (tabs then look up
  // their logins again).
  bool bitwarden_unlocked() const;
  int bitwarden_generation() const;
  using UnlockStateDone = std::function<void(BitwardenUnlockState)>;
  void BitwardenState(UnlockStateDone done);
  // Unlocks with the master password (also the "re-prompt" check, which
  // starts a new session). With "Unlock with Touch ID", the session key is
  // then kept in the keychain.
  void UnlockBitwarden(std::string master_password, ErrorDone done);
  // Unlocks with the kept session key, after the user passed Touch ID.
  void UnlockBitwardenWithStoredSession(ErrorDone done);
  // Locks now (the passwords page; the Mac sleeps or the screen locks).
  void LockBitwarden();
  using LocationsDone =
      std::function<void(Result<std::vector<BitwardenSaveLocation>>)>;
  void BitwardenSaveLocations(LocationsDone done);

 private:
  struct State;  // Only used on the password thread.
  // What the UI thread knows of the Bitwarden vault; UI thread only.
  struct Mirror {
    bool unlocked = false;
    int generation = 0;
  };
  void Post(std::function<void(State&)> work);
  // Checks every minute whether the vault has been idle long enough.
  void ScheduleIdleCheck();

  CefRefPtr<CefThread> thread_;
  std::shared_ptr<State> state_;
  std::shared_ptr<Mirror> mirror_ = std::make_shared<Mirror>();
  PasswordOptions options_;
  std::function<bool(const ProfileId&)> offer_to_save_;
  std::function<bool(const ProfileId&)> uses_bitwarden_;
};

}  // namespace brouk

#endif  // BROUK_CEF_PASSWORD_MANAGER_H_
