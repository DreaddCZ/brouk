#include "core/bitwarden_cli.h"

#include <utility>

#include "core/crypto.h"
#include "core/json.h"

namespace brouk {

namespace {

// Where Homebrew puts programs (Apple silicon, Intel).
constexpr std::string_view kHomebrewFolders[] = {"/opt/homebrew/bin",
                                                 "/usr/local/bin"};

// Variables Brouk's environment must not pass on to `bw`: other sessions
// and credentials, and Node.js options that would change how it runs.
constexpr std::string_view kRemovedVariables[] = {
    "BW_SESSION",      "BW_PASSWORD",  "BW_CLIENTID",
    "BW_CLIENTSECRET", "NODE_OPTIONS", "NODE_EXTRA_CA_CERTS"};

constexpr std::chrono::milliseconds kTimeout{60000};
constexpr std::chrono::milliseconds kSyncTimeout{180000};

std::string_view Trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\n' ||
                           text.front() == '\r' || text.front() == '\t')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\n' ||
                           text.back() == '\r' || text.back() == '\t')) {
    text.remove_suffix(1);
  }
  return text;
}

// The message of a failed command: its last line that is not part of a
// stack trace, at most 300 characters.
std::string FailureMessage(const ProcessOutput& output) {
  std::string_view text = output.err.empty() ? output.out : output.err;
  std::string_view message;
  while (!text.empty()) {
    std::size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    if (!line.empty() && line.front() != ' ' && line.front() != '\t' &&
        !Trim(line).empty()) {
      message = Trim(line);
    }
    if (end == std::string_view::npos) {
      break;
    }
    text.remove_prefix(end + 1);
  }
  return std::string(message.substr(0, 300));
}

Error ErrorOf(const ProcessOutput& output) {
  if (output.timed_out) {
    return Error{"Bitwarden did not answer in time."};
  }
  std::string message = FailureMessage(output);
  if (message.find("Vault is locked") != std::string::npos) {
    return Error{std::string(kBitwardenLockedError)};
  }
  if (message.find("not logged in") != std::string::npos) {
    return Error{"Bitwarden is not signed in."};
  }
  if (message.find("Invalid master password") != std::string::npos) {
    return Error{"The master password is not correct."};
  }
  if (message.empty()) {
    message = "exit code " + std::to_string(output.exit_code);
  }
  return Error{"Bitwarden failed: " + message};
}

}  // namespace

std::optional<std::filesystem::path> FindBitwardenCli(
    std::string_view setting,
    std::string_view search_path,
    const std::function<bool(const std::filesystem::path&)>& exists) {
  if (!Trim(setting).empty()) {
    std::filesystem::path path(Trim(setting));
    if (exists(path)) {
      return path;
    }
    return std::nullopt;
  }
  std::vector<std::filesystem::path> folders(std::begin(kHomebrewFolders),
                                             std::end(kHomebrewFolders));
  while (!search_path.empty()) {
    std::size_t end = search_path.find(':');
    std::string_view folder = search_path.substr(0, end);
    if (!folder.empty() && folder.front() == '/') {
      folders.emplace_back(folder);
    }
    if (end == std::string_view::npos) {
      break;
    }
    search_path.remove_prefix(end + 1);
  }
  for (const std::filesystem::path& folder : folders) {
    if (exists(folder / "bw")) {
      return folder / "bw";
    }
  }
  return std::nullopt;
}

BitwardenCli::BitwardenCli(std::filesystem::path program, Runner run)
    : program_(std::move(program)), run_(std::move(run)) {}

Result<std::string> BitwardenCli::Run(
    std::vector<std::string> arguments,
    std::string_view session,
    std::vector<std::pair<std::string, std::string>> secret_environment,
    std::string input) {
  ProcessRequest request;
  request.program = program_;
  request.arguments = std::move(arguments);
  request.arguments.emplace_back("--nointeraction");
  request.remove_environment.assign(std::begin(kRemovedVariables),
                                    std::end(kRemovedVariables));
  request.environment = std::move(secret_environment);
  if (!session.empty()) {
    request.environment.emplace_back("BW_SESSION", std::string(session));
  }
  if (!certificates_.empty()) {
    request.environment.emplace_back("NODE_EXTRA_CA_CERTS",
                                     certificates_.string());
  }
  // A `bw` installed with npm starts with "#!/usr/bin/env node": node is
  // usually next to it. Brouk itself starts with a minimal PATH.
  request.environment.emplace_back(
      "PATH", program_.parent_path().string() +
                  ":/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin");
  request.input = std::move(input);
  request.timeout =
      request.arguments.front() == "sync" ? kSyncTimeout : kTimeout;
  Result<ProcessOutput> result = run_(request);
  for (auto& [name, value] : request.environment) {
    Wipe(value);
  }
  Wipe(request.input);
  if (!result) {
    return result.error();
  }
  ProcessOutput& output = result.value();
  if (output.timed_out || output.exit_code != 0) {
    Error error = ErrorOf(output);
    Wipe(output.out);
    Wipe(output.err);
    return error;
  }
  Wipe(output.err);
  return std::move(output.out);
}

Result<BitwardenStatus> BitwardenCli::Status() {
  Result<std::string> out = Run({"status"}, "");
  if (!out) {
    return out.error();
  }
  std::optional<BitwardenStatus> status = ParseBitwardenStatus(out.value());
  if (!status) {
    return Error{"Bitwarden returned an unexpected status."};
  }
  return *status;
}

Result<std::string> BitwardenCli::Unlock(std::string_view master_password) {
  Result<std::string> out =
      Run({"unlock", "--passwordenv", "BW_PASSWORD", "--raw"}, "",
          {{"BW_PASSWORD", std::string(master_password)}});
  if (!out) {
    return out.error();
  }
  std::string session(Trim(out.value()));
  Wipe(out.value());
  // A session key is base64; anything else is not one.
  if (session.empty() || session.find_first_of(" \n{") != std::string::npos) {
    Wipe(session);
    return Error{"Bitwarden returned no session key."};
  }
  return session;
}

std::optional<Error> BitwardenCli::Lock() {
  Result<std::string> out = Run({"lock"}, "");
  if (!out) {
    return out.error();
  }
  return std::nullopt;
}

std::optional<Error> BitwardenCli::Sync(std::string_view session) {
  Result<std::string> out = Run({"sync"}, session);
  if (!out) {
    return out.error();
  }
  return std::nullopt;
}

Result<std::vector<BitwardenLogin>> BitwardenCli::ListLogins(
    std::string_view session) {
  Result<std::string> out = Run({"list", "items"}, session);
  if (!out) {
    return out.error();
  }
  Result<std::vector<BitwardenLogin>> logins =
      ParseBitwardenLogins(out.value());
  Wipe(out.value());
  return logins;
}

Result<std::vector<BitwardenFolder>> BitwardenCli::ListFolders(
    std::string_view session) {
  Result<std::string> out = Run({"list", "folders"}, session);
  if (!out) {
    return out.error();
  }
  return ParseBitwardenFolders(out.value());
}

Result<std::vector<BitwardenCollection>> BitwardenCli::ListCollections(
    std::string_view session) {
  Result<std::string> out = Run({"list", "collections"}, session);
  if (!out) {
    return out.error();
  }
  return ParseBitwardenCollections(out.value());
}

Result<std::map<std::string, std::string, std::less<>>>
BitwardenCli::ListOrganizations(std::string_view session) {
  Result<std::string> out = Run({"list", "organizations"}, session);
  if (!out) {
    return out.error();
  }
  return ParseBitwardenOrganizations(out.value());
}

Result<std::string> BitwardenCli::GetItem(std::string_view session,
                                          std::string_view item_id) {
  return Run({"get", "item", std::string(item_id)}, session);
}

Result<std::string> BitwardenCli::CreateItem(std::string_view session,
                                             std::string_view item_json) {
  Result<std::string> out =
      Run({"create", "item"}, session, {}, Base64Encode(item_json));
  if (!out) {
    return out.error();
  }
  Result<JsonValue> item = ParseJsonDocument(out.value());
  Wipe(out.value());
  std::string id = item ? item.value().StringOr("id", "") : "";
  if (id.empty()) {
    return Error{"Bitwarden did not confirm the new item."};
  }
  if (item) {
    // The answer repeats the password.
    for (JsonMember& member : item.value().members) {
      if (member.key == "login") {
        for (JsonMember& field : member.value.members) {
          Wipe(field.value.string);
        }
      }
    }
  }
  return id;
}

std::optional<Error> BitwardenCli::EditItem(std::string_view session,
                                            std::string_view item_id,
                                            std::string_view item_json) {
  Result<std::string> out = Run({"edit", "item", std::string(item_id)}, session,
                                {}, Base64Encode(item_json));
  if (!out) {
    return out.error();
  }
  Wipe(out.value());
  return std::nullopt;
}

}  // namespace brouk
