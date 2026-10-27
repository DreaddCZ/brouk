#ifndef BROUK_CORE_BITWARDEN_VAULT_H_
#define BROUK_CORE_BITWARDEN_VAULT_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/password_store.h"
#include "core/result.h"

namespace brouk {

// What the Bitwarden password source (008) reads from the Bitwarden CLI
// (`bw`) and how it matches pages. `bw` prints JSON; its output is treated
// as untrusted: anything of an unexpected shape is skipped, never trusted.

// `bw status`.
struct BitwardenStatus {
  enum class State { kUnauthenticated, kLocked, kUnlocked };
  State state = State::kUnauthenticated;
  std::string server_url;  // Empty: Bitwarden's US cloud.
  std::string user_email;
  std::string user_id;  // Several profiles share an unlock by this.
};
std::optional<BitwardenStatus> ParseBitwardenStatus(std::string_view json);

// How a login URI is matched against a page (Bitwarden's "match detection").
enum class BitwardenUriMatch {
  kDefault,  // Not set on the item: Bitwarden's default, the base domain.
  kDomain,
  kHost,
  kStartsWith,
  kExact,
  kRegularExpression,
  kNever,
};

struct BitwardenUri {
  std::string uri;
  BitwardenUriMatch match = BitwardenUriMatch::kDefault;
};

// A login item of the vault. Other item types (cards, notes, identities)
// are left out (008, out of scope).
struct BitwardenLogin {
  std::string id;
  std::string name;
  std::string username;
  std::string password;  // Kept in memory only, never written to disk.
  std::string totp;      // The item's TOTP field (otpauth:// or base32), or "".
  std::vector<BitwardenUri> uris;
  bool reprompt = false;
  std::string folder_id;        // "" for no folder.
  std::string organization_id;  // "" for the user's own vault.
};

// `bw list items`: the login items, without those in the trash.
Result<std::vector<BitwardenLogin>> ParseBitwardenLogins(std::string_view json);

// Where a new login can be saved: a folder of the user's own vault, or a
// collection of an organisation.
struct BitwardenFolder {
  std::string id;  // "" for "No folder".
  std::string name;
};
struct BitwardenCollection {
  std::string id;
  std::string organization_id;
  std::string name;
};
// `bw list folders`; Bitwarden lists "No folder" itself (id null).
Result<std::vector<BitwardenFolder>> ParseBitwardenFolders(
    std::string_view json);
// `bw list collections`, without those the user may not add items to.
Result<std::vector<BitwardenCollection>> ParseBitwardenCollections(
    std::string_view json);
// `bw list organizations`: organisation names by id.
Result<std::map<std::string, std::string, std::less<>>>
ParseBitwardenOrganizations(std::string_view json);

// Whether one of the login's URIs matches the page (its full URL: some
// match settings look at the path), following each URI's match setting as
// Bitwarden does: the base domain matches every host of the site, and the
// scheme does not count.
bool BitwardenLoginMatches(const BitwardenLogin& login,
                           std::string_view page_url);

// The JSON for `bw create item` (before base64 encoding): a login for
// `entry`'s origin, user name, password and 2FA secret, named after its host
// as Bitwarden's own extension does.
std::string NewBitwardenItemJson(const PasswordEntry& entry,
                                 std::string_view folder_id,
                                 std::string_view organization_id,
                                 std::string_view collection_id);
// The JSON for `bw edit item`: `item_json` (from `bw get item`) with the
// user name, password and 2FA secret of `entry`; everything else is kept.
Result<std::string> EditedBitwardenItemJson(std::string_view item_json,
                                            const PasswordEntry& entry);

// The vault's logins, in memory while it is unlocked. Each login gets a
// number for the rest of the browser (PasswordEntry::id), which stays the
// same while Brouk runs, also across refreshes. Not thread-safe.
class BitwardenVault {
 public:
  BitwardenVault() = default;
  ~BitwardenVault();
  BitwardenVault(const BitwardenVault&) = delete;
  BitwardenVault& operator=(const BitwardenVault&) = delete;

  // Replaces the logins (after `bw list items`).
  void Replace(std::vector<BitwardenLogin> logins);
  // Forgets every login and wipes the passwords (the vault locked).
  void Clear();
  bool empty() const { return logins_.empty(); }

  // The logins for a page (its full URL), ordered by user name. All are
  // "exact" matches, filled automatically where the page allows it: the
  // item's match settings decide where it belongs.
  PasswordMatches Find(std::string_view page_url) const;
  // The Bitwarden id of an entry, or nullopt if it is not in the vault.
  std::optional<std::string> ItemId(std::int64_t id) const;
  std::optional<std::int64_t> EntryId(std::string_view item_id) const;

 private:
  std::int64_t NumberFor(const std::string& item_id);

  std::vector<BitwardenLogin> logins_;
  std::map<std::string, std::int64_t, std::less<>> numbers_;
  std::int64_t next_number_ = 1;
};

}  // namespace brouk

#endif  // BROUK_CORE_BITWARDEN_VAULT_H_
