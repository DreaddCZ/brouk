#ifndef BROUK_CORE_BITWARDEN_CLI_H_
#define BROUK_CORE_BITWARDEN_CLI_H_

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/bitwarden_vault.h"
#include "core/process_runner.h"
#include "core/result.h"

namespace brouk {

// The message of an error that means the vault is locked: the session key
// is no longer valid (Bitwarden was locked or unlocked elsewhere), so the
// user unlocks again.
inline constexpr std::string_view kBitwardenLockedError =
    "The Bitwarden vault is locked.";

// Where the Bitwarden CLI is: `setting` (the "Bitwarden CLI" setting) if not
// empty, else the first `bw` in Homebrew's folders and then `search_path`
// (a PATH-style list; on Windows `bw.exe`, and no Homebrew). nullopt if there
// is none. `exists` checks a file.
std::optional<std::filesystem::path> FindBitwardenCli(
    std::string_view setting,
    std::string_view search_path,
    const std::function<bool(const std::filesystem::path&)>& exists);

// The Bitwarden CLI's commands that the Bitwarden password source (008)
// uses. Runs `bw` directly for each command, never `bw serve` (which would
// open the unlocked vault to every local program). Secrets go to `bw` in
// environment variables or standard input, never as arguments. Blocks:
// used on the password thread only.
class BitwardenCli {
 public:
  using Runner = std::function<Result<ProcessOutput>(const ProcessRequest&)>;

  // `run` is RunProcess, or a fake in tests.
  BitwardenCli(std::filesystem::path program, Runner run);

  const std::filesystem::path& program() const { return program_; }
  // A PEM file with the certificate authorities of a company's server, for
  // `bw` (Node.js does not use the macOS keychain); empty for none.
  void SetCertificates(std::filesystem::path pem) {
    certificates_ = std::move(pem);
  }

  Result<BitwardenStatus> Status();
  // The session key for the other commands. The caller wipes it.
  Result<std::string> Unlock(std::string_view master_password);
  // Ends the session: every session key stops working.
  std::optional<Error> Lock();
  std::optional<Error> Sync(std::string_view session);

  Result<std::vector<BitwardenLogin>> ListLogins(std::string_view session);
  Result<std::vector<BitwardenFolder>> ListFolders(std::string_view session);
  Result<std::vector<BitwardenCollection>> ListCollections(
      std::string_view session);
  Result<std::map<std::string, std::string, std::less<>>> ListOrganizations(
      std::string_view session);

  // An item's JSON, for EditItem. The caller wipes it.
  Result<std::string> GetItem(std::string_view session,
                              std::string_view item_id);
  // Creates an item from its JSON (NewBitwardenItemJson); returns its id.
  Result<std::string> CreateItem(std::string_view session,
                                 std::string_view item_json);
  std::optional<Error> EditItem(std::string_view session,
                                std::string_view item_id,
                                std::string_view item_json);

 private:
  // Runs `bw <arguments> --nointeraction`. The output is the program's
  // standard output when it succeeded.
  Result<std::string> Run(
      std::vector<std::string> arguments,
      std::string_view session,
      std::vector<std::pair<std::string, std::string>> secret_environment = {},
      std::string input = {});

  std::filesystem::path program_;
  Runner run_;
  std::filesystem::path certificates_;
};

}  // namespace brouk

#endif  // BROUK_CORE_BITWARDEN_CLI_H_
