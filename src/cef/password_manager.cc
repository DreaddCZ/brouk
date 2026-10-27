#include "cef/password_manager.h"

#include <sys/stat.h>

#include <chrono>
#include <map>
#include <utility>

#include "cef/function_task.h"
#include "core/crypto.h"
#include "core/process_runner.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

std::int64_t Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void PostToUi(std::function<void()> function) {
  CefPostTask(TID_UI, new FunctionTask(std::move(function)));
}

// The keychain account of a Bitwarden session key, kept for "Unlock with
// Touch ID" (one per Bitwarden account).
std::string BitwardenSessionAccount(std::string_view user_id) {
  return "bitwarden-session-" + std::string(user_id);
}

bool IsExecutableFile(const std::filesystem::path& path) {
  struct stat info{};
  return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
         (info.st_mode & S_IXUSR) != 0;
}

constexpr std::int64_t kIdleCheckMilliseconds = std::int64_t{60} * 1000;

}  // namespace

struct PasswordManager::State {
  std::function<std::filesystem::path(const ProfileId&)> database_path;
  std::shared_ptr<SecretStore> keychain;
  std::map<ProfileId, std::unique_ptr<PasswordStore>, std::less<>> open;
  // Why a profile's passwords could not be opened; retried on next use.
  std::string last_error;

  BitwardenSource::Certificates certificates;
  std::unique_ptr<BitwardenSource> bitwarden;
  BitwardenSource::Options bitwarden_options;
  bool touch_id = true;
  // The account whose session key is in the keychain ("" for none known).
  std::string user_id;

  PasswordStore* Store(const ProfileId& profile) {
    auto it = open.find(profile);
    if (it != open.end()) {
      return it->second.get();
    }
    Result<SecretKey> key =
        LoadOrCreateKey(*keychain, PasswordKeyAccount(profile));
    if (!key) {
      last_error = "The keychain could not be read: " + key.error().message;
      LOG(ERROR) << "Passwords of profile " << profile << ": " << last_error;
      return nullptr;
    }
    Result<std::unique_ptr<PasswordStore>> store =
        PasswordStore::Open(database_path(profile), key.value());
    if (!store) {
      last_error = store.error().message;
      LOG(ERROR) << "Passwords of profile " << profile << ": " << last_error;
      return nullptr;
    }
    return open.emplace(profile, std::move(store).value()).first->second.get();
  }

  // Locks Bitwarden. Without Touch ID nothing outlives the lock, so `bw`'s
  // session ends too.
  void LockBitwarden() {
    bitwarden->Lock(!touch_id);
    if (!touch_id && !user_id.empty()) {
      keychain->Remove(BitwardenSessionAccount(user_id));
    }
  }
};

PasswordManager::PasswordManager(
    std::function<std::filesystem::path(const ProfileId&)> database_path,
    std::shared_ptr<SecretStore> keychain)
    : thread_(CefThread::CreateThread("BroukPasswords")),
      state_(std::make_shared<State>()) {
  CEF_REQUIRE_UI_THREAD();
  state_->database_path = std::move(database_path);
  state_->keychain = std::move(keychain);
  state_->bitwarden = std::make_unique<BitwardenSource>(
      RunProcess, IsExecutableFile,
      [state = state_.get()](const std::string& server) {
        return state->certificates ? state->certificates(server)
                                   : std::filesystem::path();
      });
  ScheduleIdleCheck();
}

PasswordManager::~PasswordManager() {
  CEF_REQUIRE_UI_THREAD();
  Post([](State& state) {
    state.open.clear();
    if (state.bitwarden->unlocked()) {
      state.LockBitwarden();
    }
  });
  thread_->Stop();
}

void PasswordManager::Post(std::function<void(State&)> work) {
  std::shared_ptr<State> state = state_;
  thread_->GetTaskRunner()->PostTask(
      new FunctionTask([state, work = std::move(work)] { work(*state); }));
}

void PasswordManager::SetOptions(const PasswordOptions& options) {
  CEF_REQUIRE_UI_THREAD();
  options_ = options;
  std::string cli = options.bitwarden_cli;
  std::int64_t lock_seconds = options.bitwarden_lock_minutes * 60;
  bool touch_id = options.bitwarden_touch_id;
  Post([cli = std::move(cli), lock_seconds, touch_id](State& state) mutable {
    state.bitwarden_options.cli_setting = std::move(cli);
    state.bitwarden_options.lock_seconds = lock_seconds;
    state.bitwarden->SetOptions(state.bitwarden_options);
    if (state.touch_id && !touch_id && !state.user_id.empty()) {
      // Touch ID turned off: no session key stays in the keychain.
      state.keychain->Remove(BitwardenSessionAccount(state.user_id));
    }
    state.touch_id = touch_id;
  });
}

void PasswordManager::SetBitwarden(
    std::function<bool(const ProfileId&)> uses_bitwarden,
    BitwardenSource::Certificates certificates,
    std::string search_path) {
  CEF_REQUIRE_UI_THREAD();
  uses_bitwarden_ = std::move(uses_bitwarden);
  Post([certificates = std::move(certificates),
        search_path = std::move(search_path)](State& state) mutable {
    state.certificates = std::move(certificates);
    state.bitwarden_options.search_path = std::move(search_path);
    state.bitwarden->SetOptions(state.bitwarden_options);
  });
}

bool PasswordManager::UsesBitwarden(const ProfileId& profile) const {
  return uses_bitwarden_ && uses_bitwarden_(profile);
}

void PasswordManager::ScheduleIdleCheck() {
  std::shared_ptr<State> state = state_;
  std::weak_ptr<Mirror> mirror = mirror_;
  thread_->GetTaskRunner()->PostDelayedTask(
      new FunctionTask([this, state, mirror] {
        if (state->bitwarden->IsIdle(Now())) {
          state->LockBitwarden();
          PostToUi([mirror] {
            if (std::shared_ptr<Mirror> m = mirror.lock()) {
              m->unlocked = false;
              ++m->generation;
            }
          });
        }
        // The manager outlives its thread (the destructor stops it), so
        // `this` is valid while tasks run.
        ScheduleIdleCheck();
      }),
      kIdleCheckMilliseconds);
}

void PasswordManager::Lookup(const ProfileId& profile,
                             std::string origin,
                             std::string url,
                             LookupDone done) {
  CEF_REQUIRE_UI_THREAD();
  bool offer = !offer_to_save_ || offer_to_save_(profile);
  bool bitwarden = UsesBitwarden(profile);
  Post([profile, origin = std::move(origin), url = std::move(url), offer,
        bitwarden, done = std::move(done)](State& state) mutable {
    auto lookup = std::make_shared<PasswordLookup>();
    lookup->offer_to_save = offer;
    // "Never for this site" is kept in the profile's own database for both
    // sources.
    PasswordStore* store = state.Store(profile);
    if (bitwarden) {
      lookup->locked = !state.bitwarden->unlocked();
      lookup->matches = state.bitwarden->Find(url, Now());
      lookup->never_save = store != nullptr && store->IsNeverSave(origin);
    } else if (store != nullptr) {
      lookup->matches = store->Find(origin);
      lookup->never_save = store->IsNeverSave(origin);
    } else {
      lookup->error = state.last_error;
    }
    PostToUi([lookup, done = std::move(done)] { done(std::move(*lookup)); });
  });
}

void PasswordManager::Add(const ProfileId& profile,
                          PasswordEntry entry,
                          SaveDone done,
                          BitwardenSaveLocation location) {
  CEF_REQUIRE_UI_THREAD();
  bool bitwarden = UsesBitwarden(profile);
  Post([profile, entry = std::move(entry), done = std::move(done),
        location = std::move(location), bitwarden](State& state) mutable {
    auto result = std::make_shared<Result<std::int64_t>>(
        Error{"The passwords could not be opened."});
    if (bitwarden) {
      *result = state.bitwarden->Add(entry, location, Now());
    } else if (PasswordStore* store = state.Store(profile)) {
      *result = store->Add(entry, Now());
    }
    Wipe(entry.password);
    PostToUi([result, done = std::move(done)] {
      if (done) {
        done(std::move(*result));
      }
    });
  });
}

void PasswordManager::Update(const ProfileId& profile,
                             PasswordEntry entry,
                             ErrorDone done) {
  CEF_REQUIRE_UI_THREAD();
  bool bitwarden = UsesBitwarden(profile);
  Post([profile, entry = std::move(entry), done = std::move(done),
        bitwarden](State& state) mutable {
    std::optional<Error> error = Error{"The passwords could not be opened."};
    if (bitwarden) {
      error = state.bitwarden->Update(entry, Now());
    } else if (PasswordStore* store = state.Store(profile)) {
      error = store->Update(entry, Now());
    }
    Wipe(entry.password);
    PostToUi([error = std::move(error), done = std::move(done)] {
      if (done) {
        done(error);
      }
    });
  });
}

void PasswordManager::MarkUsed(const ProfileId& profile, std::int64_t id) {
  CEF_REQUIRE_UI_THREAD();
  if (UsesBitwarden(profile)) {
    return;  // Bitwarden entries are not counted.
  }
  Post([profile, id](State& state) {
    if (PasswordStore* store = state.Store(profile)) {
      store->MarkUsed(id, Now());
    }
  });
}

void PasswordManager::SetNeverSave(const ProfileId& profile,
                                   std::string origin,
                                   bool never) {
  CEF_REQUIRE_UI_THREAD();
  Post([profile, origin = std::move(origin), never](State& state) {
    if (PasswordStore* store = state.Store(profile)) {
      if (never) {
        store->AddNeverSave(origin);
      } else {
        store->RemoveNeverSave(origin);
      }
    }
  });
}

void PasswordManager::Run(
    const ProfileId& profile,
    std::function<void(PasswordStore&)> work,
    std::function<void(std::optional<std::string>)> done) {
  CEF_REQUIRE_UI_THREAD();
  Post([profile, work = std::move(work),
        done = std::move(done)](State& state) mutable {
    std::optional<std::string> failed;
    if (PasswordStore* store = state.Store(profile)) {
      work(*store);
    } else {
      failed = state.last_error;
    }
    PostToUi([failed = std::move(failed), done = std::move(done)] {
      if (done) {
        done(failed);
      }
    });
  });
}

void PasswordManager::DeleteProfile(const ProfileId& profile) {
  CEF_REQUIRE_UI_THREAD();
  Post([profile](State& state) {
    state.open.erase(profile);
    state.keychain->Remove(PasswordKeyAccount(profile));
  });
}

bool PasswordManager::bitwarden_unlocked() const {
  CEF_REQUIRE_UI_THREAD();
  return mirror_->unlocked;
}

int PasswordManager::bitwarden_generation() const {
  CEF_REQUIRE_UI_THREAD();
  return mirror_->generation;
}

void PasswordManager::BitwardenState(UnlockStateDone done) {
  CEF_REQUIRE_UI_THREAD();
  std::weak_ptr<Mirror> mirror = mirror_;
  Post([done = std::move(done), mirror](State& state) mutable {
    auto answer = std::make_shared<BitwardenUnlockState>();
    answer->status = state.bitwarden->CheckStatus();
    const BitwardenSource::Status& status = answer->status;
    if (!status.user_id.empty()) {
      state.user_id = status.user_id;
    }
    if (status.state == BitwardenSource::State::kNoCli ||
        status.state == BitwardenSource::State::kLoggedOut) {
      std::filesystem::path certificates;
      if (state.certificates && !status.server_url.empty()) {
        certificates = state.certificates(status.server_url);
      }
      answer->setup_steps = BitwardenSetupSteps(status, certificates);
    } else if (state.touch_id && !status.user_id.empty()) {
      Result<std::optional<std::string>> kept =
          state.keychain->Read(BitwardenSessionAccount(status.user_id));
      answer->stored_session = kept && kept.value().has_value();
      if (kept && kept.value()) {
        Wipe(*kept.value());
      }
    }
    bool unlocked = state.bitwarden->unlocked();
    PostToUi([answer, done = std::move(done), mirror, unlocked] {
      if (std::shared_ptr<Mirror> m = mirror.lock();
          m && m->unlocked != unlocked) {
        m->unlocked = unlocked;
        ++m->generation;
      }
      done(std::move(*answer));
    });
  });
}

void PasswordManager::UnlockBitwarden(std::string master_password,
                                      ErrorDone done) {
  CEF_REQUIRE_UI_THREAD();
  std::weak_ptr<Mirror> mirror = mirror_;
  Post([password = std::move(master_password), done = std::move(done),
        mirror](State& state) mutable {
    std::optional<Error> error = state.bitwarden->Unlock(password, Now());
    Wipe(password);
    if (!error && state.touch_id) {
      BitwardenSource::Status status = state.bitwarden->CheckStatus();
      state.user_id = status.user_id;
      if (!status.user_id.empty()) {
        state.keychain->Write(BitwardenSessionAccount(status.user_id),
                              state.bitwarden->session());
      }
    }
    bool unlocked = state.bitwarden->unlocked();
    PostToUi(
        [error = std::move(error), done = std::move(done), mirror, unlocked] {
          if (std::shared_ptr<Mirror> m = mirror.lock()) {
            m->unlocked = unlocked;
            ++m->generation;
          }
          if (done) {
            done(error);
          }
        });
  });
  // Pulls the latest vault from the server once unlocked.
  Post([](State& state) {
    if (state.bitwarden->unlocked()) {
      state.bitwarden->Sync(Now());
    }
  });
}

void PasswordManager::UnlockBitwardenWithStoredSession(ErrorDone done) {
  CEF_REQUIRE_UI_THREAD();
  std::weak_ptr<Mirror> mirror = mirror_;
  Post([done = std::move(done), mirror](State& state) mutable {
    std::optional<Error> error = Error{std::string(kBitwardenLockedError)};
    if (!state.user_id.empty()) {
      std::string account = BitwardenSessionAccount(state.user_id);
      Result<std::optional<std::string>> kept = state.keychain->Read(account);
      if (kept && kept.value()) {
        error = state.bitwarden->UseSession(std::move(*kept.value()), Now());
        if (error) {
          // No longer valid (locked or unlocked elsewhere): forgotten, so
          // the user is asked for the master password.
          state.keychain->Remove(account);
        }
      }
    }
    bool unlocked = state.bitwarden->unlocked();
    PostToUi(
        [error = std::move(error), done = std::move(done), mirror, unlocked] {
          if (std::shared_ptr<Mirror> m = mirror.lock()) {
            m->unlocked = unlocked;
            ++m->generation;
          }
          if (done) {
            done(error);
          }
        });
  });
  Post([](State& state) {
    if (state.bitwarden->unlocked()) {
      state.bitwarden->Sync(Now());
    }
  });
}

void PasswordManager::LockBitwarden() {
  CEF_REQUIRE_UI_THREAD();
  mirror_->unlocked = false;
  ++mirror_->generation;
  Post([](State& state) {
    if (state.bitwarden->unlocked()) {
      state.LockBitwarden();
    }
  });
}

void PasswordManager::BitwardenSaveLocations(LocationsDone done) {
  CEF_REQUIRE_UI_THREAD();
  Post([done = std::move(done)](State& state) mutable {
    auto locations =
        std::make_shared<Result<std::vector<BitwardenSaveLocation>>>(
            state.bitwarden->SaveLocations(Now()));
    PostToUi(
        [locations, done = std::move(done)] { done(std::move(*locations)); });
  });
}

}  // namespace brouk
