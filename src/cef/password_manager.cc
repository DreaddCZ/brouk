#include "cef/password_manager.h"

#include <chrono>
#include <map>
#include <utility>

#include "cef/function_task.h"
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

}  // namespace

struct PasswordManager::State {
  std::function<std::filesystem::path(const ProfileId&)> database_path;
  std::shared_ptr<SecretStore> keychain;
  std::map<ProfileId, std::unique_ptr<PasswordStore>, std::less<>> open;
  // Why a profile's passwords could not be opened; retried on next use.
  std::string last_error;

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
};

PasswordManager::PasswordManager(
    std::function<std::filesystem::path(const ProfileId&)> database_path,
    std::shared_ptr<SecretStore> keychain)
    : thread_(CefThread::CreateThread("BroukPasswords")),
      state_(std::make_shared<State>()) {
  CEF_REQUIRE_UI_THREAD();
  state_->database_path = std::move(database_path);
  state_->keychain = std::move(keychain);
}

PasswordManager::~PasswordManager() {
  CEF_REQUIRE_UI_THREAD();
  Post([](State& state) { state.open.clear(); });
  thread_->Stop();
}

void PasswordManager::Post(std::function<void(State&)> work) {
  std::shared_ptr<State> state = state_;
  thread_->GetTaskRunner()->PostTask(
      new FunctionTask([state, work = std::move(work)] { work(*state); }));
}

void PasswordManager::Lookup(const ProfileId& profile,
                             std::string origin,
                             LookupDone done) {
  CEF_REQUIRE_UI_THREAD();
  bool offer = !offer_to_save_ || offer_to_save_(profile);
  Post([profile, origin = std::move(origin), offer,
        done = std::move(done)](State& state) mutable {
    auto lookup = std::make_shared<PasswordLookup>();
    lookup->offer_to_save = offer;
    if (PasswordStore* store = state.Store(profile)) {
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
                          SaveDone done) {
  CEF_REQUIRE_UI_THREAD();
  Post([profile, entry = std::move(entry),
        done = std::move(done)](State& state) mutable {
    auto result = std::make_shared<Result<std::int64_t>>(
        Error{"The passwords could not be opened."});
    if (PasswordStore* store = state.Store(profile)) {
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
  Post([profile, entry = std::move(entry),
        done = std::move(done)](State& state) mutable {
    std::optional<Error> error = Error{"The passwords could not be opened."};
    if (PasswordStore* store = state.Store(profile)) {
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

}  // namespace brouk
