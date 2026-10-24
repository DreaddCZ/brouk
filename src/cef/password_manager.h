#ifndef BROUK_CEF_PASSWORD_MANAGER_H_
#define BROUK_CEF_PASSWORD_MANAGER_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
};

// What a page's origin has in a profile's passwords.
struct PasswordLookup {
  PasswordMatches matches;
  bool never_save = false;
  bool offer_to_save = true;
  // The passwords could not be opened (keychain locked or key lost): no
  // filling and no saving; the message is for the user.
  std::optional<std::string> error;
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
  void SetOptions(const PasswordOptions& options) { options_ = options; }
  // Whether a profile offers to save passwords (its setting).
  void SetOfferToSave(std::function<bool(const ProfileId&)> offer_to_save) {
    offer_to_save_ = std::move(offer_to_save);
  }

  using LookupDone = std::function<void(PasswordLookup)>;
  void Lookup(const ProfileId& profile, std::string origin, LookupDone done);

  using SaveDone = std::function<void(Result<std::int64_t>)>;
  void Add(const ProfileId& profile, PasswordEntry entry, SaveDone done);
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

 private:
  struct State;  // Only used on the password thread.
  void Post(std::function<void(State&)> work);

  CefRefPtr<CefThread> thread_;
  std::shared_ptr<State> state_;
  PasswordOptions options_;
  std::function<bool(const ProfileId&)> offer_to_save_;
};

}  // namespace brouk

#endif  // BROUK_CEF_PASSWORD_MANAGER_H_
