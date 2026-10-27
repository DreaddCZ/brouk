#include "core/bitwarden_source.h"

#include <utility>

#include "core/crypto.h"

namespace brouk {

BitwardenSource::BitwardenSource(
    BitwardenCli::Runner run,
    std::function<bool(const std::filesystem::path&)> exists,
    Certificates certificates)
    : run_(std::move(run)),
      exists_(std::move(exists)),
      certificates_(std::move(certificates)) {}

BitwardenSource::~BitwardenSource() {
  Lock(false);
}

void BitwardenSource::SetOptions(Options options) {
  if (options.cli_setting != options_.cli_setting ||
      options.search_path != options_.search_path) {
    cli_.reset();
    certificates_server_.clear();
  }
  options_ = std::move(options);
}

BitwardenCli* BitwardenSource::Cli() {
  if (!cli_) {
    std::optional<std::filesystem::path> program =
        FindBitwardenCli(options_.cli_setting, options_.search_path, exists_);
    if (program) {
      cli_ = std::make_unique<BitwardenCli>(*program, run_);
    }
  }
  return cli_.get();
}

void BitwardenSource::PrepareCertificates(const std::string& server_url) {
  if (!cli_ || server_url == certificates_server_) {
    return;
  }
  certificates_server_ = server_url;
  cli_->SetCertificates(certificates_ && !server_url.empty()
                            ? certificates_(server_url)
                            : std::filesystem::path());
}

BitwardenSource::Status BitwardenSource::CheckStatus() {
  Status status;
  BitwardenCli* cli = Cli();
  if (cli == nullptr) {
    status.state = State::kNoCli;
    return status;
  }
  Result<BitwardenStatus> answer = cli->Status();
  if (!answer) {
    status.state = State::kLoggedOut;
    status.problem = answer.error().message;
    return status;
  }
  const BitwardenStatus& bw = answer.value();
  status.user_email = bw.user_email;
  status.user_id = bw.user_id;
  status.server_url = bw.server_url;
  PrepareCertificates(bw.server_url);
  if (bw.state == BitwardenStatus::State::kUnauthenticated) {
    status.state = State::kLoggedOut;
    Lock(false);
  } else {
    // `bw` itself always reports "locked" here: Brouk keeps the session.
    status.state = unlocked() ? State::kUnlocked : State::kLocked;
  }
  return status;
}

std::optional<Error> BitwardenSource::Load() {
  Result<std::vector<BitwardenLogin>> logins = cli_->ListLogins(session_);
  if (!logins) {
    return Failed(logins.error());
  }
  vault_.Replace(std::move(logins).value());
  return std::nullopt;
}

std::optional<Error> BitwardenSource::Failed(const Error& error) {
  if (error.message == kBitwardenLockedError) {
    Lock(false);
  }
  return error;
}

std::optional<Error> BitwardenSource::Unlock(std::string_view master_password,
                                             std::int64_t now) {
  Status status = CheckStatus();
  if (status.state == State::kNoCli) {
    return Error{"The Bitwarden CLI was not found."};
  }
  if (status.state == State::kLoggedOut) {
    return Error{status.problem.empty() ? "Bitwarden is not signed in."
                                        : status.problem};
  }
  Lock(false);
  Result<std::string> session = cli_->Unlock(master_password);
  if (!session) {
    return session.error();
  }
  session_ = std::move(session).value();
  last_use_ = now;
  if (std::optional<Error> error = Load()) {
    Lock(false);
    return error;
  }
  return std::nullopt;
}

std::optional<Error> BitwardenSource::UseSession(std::string session,
                                                 std::int64_t now) {
  Status status = CheckStatus();
  if (status.state != State::kLocked && status.state != State::kUnlocked) {
    Wipe(session);
    return Error{std::string(kBitwardenLockedError)};
  }
  Lock(false);
  session_ = std::move(session);
  last_use_ = now;
  if (std::optional<Error> error = Load()) {
    Lock(false);
    return error;
  }
  return std::nullopt;
}

void BitwardenSource::Lock(bool end_session) {
  vault_.Clear();
  bool had_session = !session_.empty();
  Wipe(session_);
  session_.clear();
  if (end_session && had_session && cli_) {
    cli_->Lock();
  }
}

bool BitwardenSource::IsIdle(std::int64_t now) const {
  return unlocked() && now - last_use_ >= options_.lock_seconds;
}

std::optional<Error> BitwardenSource::Sync(std::int64_t now) {
  if (!unlocked() || !cli_) {
    return Error{std::string(kBitwardenLockedError)};
  }
  last_use_ = now;
  if (std::optional<Error> error = cli_->Sync(session_)) {
    return Failed(*error);
  }
  return Load();
}

PasswordMatches BitwardenSource::Find(std::string_view page_url,
                                      std::int64_t now) {
  if (!unlocked()) {
    return {};
  }
  last_use_ = now;
  return vault_.Find(page_url);
}

Result<std::vector<BitwardenSaveLocation>> BitwardenSource::SaveLocations(
    std::int64_t now) {
  if (!unlocked() || !cli_) {
    return Error{std::string(kBitwardenLockedError)};
  }
  last_use_ = now;
  Result<std::vector<BitwardenFolder>> folders = cli_->ListFolders(session_);
  if (!folders) {
    return *Failed(folders.error());
  }
  std::vector<BitwardenSaveLocation> locations;
  locations.push_back({"No folder", "", "", ""});
  for (const BitwardenFolder& folder : folders.value()) {
    if (!folder.id.empty()) {
      locations.push_back({folder.name, folder.id, "", ""});
    }
  }
  Result<std::vector<BitwardenCollection>> collections =
      cli_->ListCollections(session_);
  auto organizations = cli_->ListOrganizations(session_);
  if (collections && organizations) {
    for (const BitwardenCollection& collection : collections.value()) {
      auto organization =
          organizations.value().find(collection.organization_id);
      std::string label = organization == organizations.value().end()
                              ? collection.name
                              : organization->second + " / " + collection.name;
      locations.push_back(
          {label, "", collection.organization_id, collection.id});
    }
  }
  return locations;
}

Result<std::int64_t> BitwardenSource::Add(const PasswordEntry& entry,
                                          const BitwardenSaveLocation& location,
                                          std::int64_t now) {
  if (!unlocked() || !cli_) {
    return Error{std::string(kBitwardenLockedError)};
  }
  last_use_ = now;
  std::string json =
      NewBitwardenItemJson(entry, location.folder_id, location.organization_id,
                           location.collection_id);
  Result<std::string> item_id = cli_->CreateItem(session_, json);
  Wipe(json);
  if (!item_id) {
    return *Failed(item_id.error());
  }
  // The new item is in `bw`'s copy already; a failed sync only delays
  // other devices' changes.
  cli_->Sync(session_);
  if (std::optional<Error> error = Load()) {
    return *error;
  }
  std::optional<std::int64_t> id = vault_.EntryId(item_id.value());
  if (!id) {
    return Error{"The new Bitwarden item could not be read back."};
  }
  return *id;
}

std::optional<Error> BitwardenSource::Update(const PasswordEntry& entry,
                                             std::int64_t now) {
  if (!unlocked() || !cli_) {
    return Error{std::string(kBitwardenLockedError)};
  }
  last_use_ = now;
  std::optional<std::string> item_id = vault_.ItemId(entry.id);
  if (!item_id) {
    return Error{"The login is no longer in the Bitwarden vault."};
  }
  Result<std::string> item = cli_->GetItem(session_, *item_id);
  if (!item) {
    return Failed(item.error());
  }
  Result<std::string> edited = EditedBitwardenItemJson(item.value(), entry);
  Wipe(item.value());
  if (!edited) {
    return edited.error();
  }
  std::optional<Error> error =
      cli_->EditItem(session_, *item_id, edited.value());
  Wipe(edited.value());
  if (error) {
    return Failed(*error);
  }
  cli_->Sync(session_);
  return Load();
}

}  // namespace brouk
