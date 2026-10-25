#include "cef/autofill_manager.h"

#include <algorithm>
#include <chrono>
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

struct AutofillManager::State {
  std::filesystem::path database;
  std::shared_ptr<SecretStore> keychain;
  std::unique_ptr<AutofillStore> store;
};

AutofillManager::AutofillManager(std::filesystem::path database,
                                 std::shared_ptr<SecretStore> keychain)
    : thread_(CefThread::CreateThread("BroukAutofill")),
      state_(std::make_shared<State>()),
      alive_(std::make_shared<bool>(true)) {
  CEF_REQUIRE_UI_THREAD();
  state_->database = std::move(database);
  state_->keychain = std::move(keychain);
  std::weak_ptr<bool> alive = alive_;
  Post([this, alive](State& state) {
    std::optional<std::string> error;
    std::vector<AutofillEntry> entries;
    std::vector<CustomField> custom;
    Result<SecretKey> key =
        LoadOrCreateKey(*state.keychain, kAutofillKeyAccount);
    if (!key) {
      error = "The keychain could not be read: " + key.error().message;
    } else {
      Result<std::unique_ptr<AutofillStore>> store =
          AutofillStore::Open(state.database, key.value());
      if (!store) {
        error = store.error().message;
      } else {
        state.store = std::move(store).value();
        entries = state.store->Entries();
        custom = state.store->CustomFields();
      }
    }
    if (error) {
      LOG(ERROR) << "Autofill: " << *error;
    }
    PostToUi([this, alive, error, entries = std::move(entries),
              custom = std::move(custom)]() mutable {
      if (alive.expired()) {
        return;
      }
      loaded_ = true;
      error_ = error;
      entries_ = std::move(entries);
      custom_ = std::move(custom);
      Changed();
    });
  });
}

AutofillManager::~AutofillManager() {
  CEF_REQUIRE_UI_THREAD();
  alive_.reset();
  Post([](State& state) { state.store.reset(); });
  thread_->Stop();
}

void AutofillManager::Post(std::function<void(State&)> work) {
  std::shared_ptr<State> state = state_;
  thread_->GetTaskRunner()->PostTask(
      new FunctionTask([state, work = std::move(work)] { work(*state); }));
}

void AutofillManager::Changed() {
  if (changed_) {
    changed_();
  }
}

void AutofillManager::AddEntry(AutofillEntry entry, IdDone done) {
  CEF_REQUIRE_UI_THREAD();
  std::weak_ptr<bool> alive = alive_;
  std::int64_t now = Now();
  Post([this, alive, entry, now, done = std::move(done)](State& state) mutable {
    Result<std::int64_t> id =
        state.store
            ? state.store->AddEntry(entry, now)
            : Result<std::int64_t>(Error{"Autofill data is not available."});
    PostToUi([this, alive, entry = std::move(entry), id, now,
              done = std::move(done)]() mutable {
      if (alive.expired()) {
        return;
      }
      if (id) {
        entry.id = id.value();
        if (entry.created == 0) {
          entry.created = now;
        }
        entries_.push_back(std::move(entry));
        Changed();
      }
      if (done) {
        done(id);
      }
    });
  });
}

void AutofillManager::UpdateEntry(const AutofillEntry& entry) {
  CEF_REQUIRE_UI_THREAD();
  auto it = std::ranges::find_if(
      entries_, [&](const AutofillEntry& e) { return e.id == entry.id; });
  if (it == entries_.end()) {
    return;
  }
  // Usage counts stay as they are.
  AutofillEntry updated = entry;
  updated.created = it->created;
  updated.last_used = it->last_used;
  updated.times_used = it->times_used;
  *it = updated;
  Post([updated](State& state) {
    if (state.store) {
      state.store->UpdateEntry(updated);
    }
  });
  Changed();
}

void AutofillManager::RemoveEntries(const std::vector<std::int64_t>& ids) {
  CEF_REQUIRE_UI_THREAD();
  removed_entries_.clear();
  removed_custom_.clear();
  std::erase_if(entries_, [&](const AutofillEntry& entry) {
    if (std::ranges::find(ids, entry.id) == ids.end()) {
      return false;
    }
    removed_entries_.push_back(entry);
    return true;
  });
  Post([ids](State& state) {
    if (state.store) {
      state.store->RemoveEntries(ids);
    }
  });
  Changed();
}

void AutofillManager::MarkEntryUsed(std::int64_t id) {
  CEF_REQUIRE_UI_THREAD();
  std::int64_t now = Now();
  for (AutofillEntry& entry : entries_) {
    if (entry.id == id) {
      entry.last_used = now;
      ++entry.times_used;
    }
  }
  Post([id, now](State& state) {
    if (state.store) {
      state.store->MarkEntryUsed(id, now);
    }
  });
}

void AutofillManager::AddCustomField(CustomField field, IdDone done) {
  CEF_REQUIRE_UI_THREAD();
  std::weak_ptr<bool> alive = alive_;
  Post([this, alive, field, done = std::move(done)](State& state) mutable {
    Result<std::int64_t> id =
        state.store
            ? state.store->AddCustomField(field)
            : Result<std::int64_t>(Error{"Autofill data is not available."});
    PostToUi([this, alive, field = std::move(field), id,
              done = std::move(done)]() mutable {
      if (alive.expired()) {
        return;
      }
      if (id) {
        field.id = id.value();
        custom_.push_back(std::move(field));
        Changed();
      }
      if (done) {
        done(id);
      }
    });
  });
}

void AutofillManager::UpdateCustomField(const CustomField& field) {
  CEF_REQUIRE_UI_THREAD();
  auto it = std::ranges::find_if(
      custom_, [&](const CustomField& f) { return f.id == field.id; });
  if (it == custom_.end()) {
    return;
  }
  CustomField updated = field;
  updated.last_used = it->last_used;
  *it = updated;
  Post([updated](State& state) {
    if (state.store) {
      state.store->UpdateCustomField(updated);
    }
  });
  Changed();
}

void AutofillManager::RemoveCustomFields(const std::vector<std::int64_t>& ids) {
  CEF_REQUIRE_UI_THREAD();
  removed_entries_.clear();
  removed_custom_.clear();
  std::erase_if(custom_, [&](const CustomField& field) {
    if (std::ranges::find(ids, field.id) == ids.end()) {
      return false;
    }
    removed_custom_.push_back(field);
    return true;
  });
  Post([ids](State& state) {
    if (state.store) {
      state.store->RemoveCustomFields(ids);
    }
  });
  Changed();
}

void AutofillManager::MarkCustomFieldUsed(std::int64_t id) {
  CEF_REQUIRE_UI_THREAD();
  std::int64_t now = Now();
  for (CustomField& field : custom_) {
    if (field.id == id) {
      field.last_used = now;
    }
  }
  Post([id, now](State& state) {
    if (state.store) {
      state.store->MarkCustomFieldUsed(id, now);
    }
  });
}

void AutofillManager::Undo() {
  CEF_REQUIRE_UI_THREAD();
  std::vector<AutofillEntry> entries = std::move(removed_entries_);
  std::vector<CustomField> custom = std::move(removed_custom_);
  removed_entries_.clear();
  removed_custom_.clear();
  entries_.insert(entries_.end(), entries.begin(), entries.end());
  custom_.insert(custom_.end(), custom.begin(), custom.end());
  std::ranges::sort(entries_, {}, &AutofillEntry::id);
  std::ranges::sort(custom_, {}, &CustomField::id);
  Post(
      [entries = std::move(entries), custom = std::move(custom)](State& state) {
        if (state.store) {
          state.store->RestoreEntries(entries);
          state.store->RestoreCustomFields(custom);
        }
      });
  Changed();
}

}  // namespace brouk
