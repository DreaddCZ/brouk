#include "cef/history_service.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr std::int64_t kSecondsPerDay = std::int64_t{24} * 60 * 60;
// How often the retention limit is applied while Brouk runs.
constexpr std::int64_t kRetentionInterval = std::int64_t{60} * 60;

std::int64_t Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// Runs a std::function as a CEF task.
class FunctionTask : public CefTask {
 public:
  explicit FunctionTask(std::function<void()> function)
      : function_(std::move(function)) {}
  void Execute() override { function_(); }

 private:
  std::function<void()> function_;
  IMPLEMENT_REFCOUNTING(FunctionTask);
};

}  // namespace

struct HistoryService::State {
  std::function<std::filesystem::path(const ProfileId&)> database_path;
  std::optional<int> retention_days = 90;
  std::vector<UrlPattern> never_record;
  struct Open {
    std::unique_ptr<HistoryStore> store;
    std::int64_t last_cleanup = 0;
  };
  std::map<ProfileId, Open, std::less<>> open;

  // The profile's database, opened on first use; null if it cannot be
  // opened (then history is simply not recorded).
  HistoryStore* Store(const ProfileId& profile) {
    auto it = open.find(profile);
    if (it == open.end()) {
      Result<std::unique_ptr<HistoryStore>> store =
          HistoryStore::Open(database_path(profile));
      if (!store) {
        LOG(ERROR) << "History of profile " << profile << ": "
                   << store.error().message;
        return nullptr;
      }
      it = open.emplace(profile, Open{std::move(store).value(), 0}).first;
      it->second.store->DeleteMatching(never_record);
    }
    ApplyRetention(it->second, false);
    return it->second.store.get();
  }

  void ApplyRetention(Open& entry, bool now) {
    std::int64_t time = Now();
    if (!retention_days ||
        (!now && time - entry.last_cleanup < kRetentionInterval)) {
      return;
    }
    entry.last_cleanup = time;
    entry.store->DeleteVisitsBefore(time - *retention_days * kSecondsPerDay);
  }
};

HistoryService::HistoryService(
    std::function<std::filesystem::path(const ProfileId&)> database_path)
    : thread_(CefThread::CreateThread("BroukHistory")),
      state_(std::make_shared<State>()) {
  CEF_REQUIRE_UI_THREAD();
  state_->database_path = std::move(database_path);
}

HistoryService::~HistoryService() {
  CEF_REQUIRE_UI_THREAD();
  // The databases close on their thread, after the work queued before.
  Post([](State& state) { state.open.clear(); });
  thread_->Stop();
}

void HistoryService::Post(std::function<void(State&)> work) {
  std::shared_ptr<State> state = state_;
  thread_->GetTaskRunner()->PostTask(
      new FunctionTask([state, work = std::move(work)] { work(*state); }));
}

void HistoryService::SetRetentionDays(std::optional<int> days) {
  CEF_REQUIRE_UI_THREAD();
  Post([days](State& state) {
    bool changed = state.retention_days != days;
    state.retention_days = days;
    if (changed) {
      for (auto& [profile, entry] : state.open) {
        state.ApplyRetention(entry, true);
      }
    }
  });
}

void HistoryService::SetNeverRecord(std::vector<UrlPattern> patterns) {
  CEF_REQUIRE_UI_THREAD();
  Post([patterns = std::move(patterns)](State& state) {
    state.never_record = patterns;
    for (auto& [profile, entry] : state.open) {
      entry.store->DeleteMatching(state.never_record);
    }
  });
}

void HistoryService::RemoveRedirectSource(const ProfileId& profile,
                                          std::string url) {
  CEF_REQUIRE_UI_THREAD();
  Post([profile, url = std::move(url)](State& state) {
    if (HistoryStore* store = state.Store(profile)) {
      store->RemoveLastVisit(url);
    }
  });
}

void HistoryService::RecordVisit(const ProfileId& profile,
                                 std::string url,
                                 std::string title,
                                 VisitTransition transition) {
  CEF_REQUIRE_UI_THREAD();
  if (!ShouldRecordInHistory(url)) {
    return;
  }
  std::int64_t time = Now();
  Post([profile, url = std::move(url), title = std::move(title), time,
        transition](State& state) {
    if (MatchesAnyPattern(state.never_record, url)) {
      return;
    }
    if (HistoryStore* store = state.Store(profile)) {
      if (std::optional<Error> error =
              store->RecordVisit(url, title, time, transition)) {
        LOG(ERROR) << error->message;
      }
    }
  });
}

void HistoryService::SetTitle(const ProfileId& profile,
                              std::string url,
                              std::string title) {
  CEF_REQUIRE_UI_THREAD();
  if (!ShouldRecordInHistory(url)) {
    return;
  }
  Post([profile, url = std::move(url), title = std::move(title)](State& state) {
    if (MatchesAnyPattern(state.never_record, url)) {
      return;
    }
    if (HistoryStore* store = state.Store(profile)) {
      store->SetTitle(url, title);
    }
  });
}

void HistoryService::DeletePage(const ProfileId& profile, std::string url) {
  CEF_REQUIRE_UI_THREAD();
  Post([profile, url = std::move(url)](State& state) {
    if (HistoryStore* store = state.Store(profile)) {
      store->DeletePage(url);
    }
  });
}

void HistoryService::CloseProfile(const ProfileId& profile) {
  CEF_REQUIRE_UI_THREAD();
  Post([profile](State& state) { state.open.erase(profile); });
}

void HistoryService::RecentPages(const ProfileId& profile,
                                 std::size_t limit,
                                 SuggestDone done) {
  CEF_REQUIRE_UI_THREAD();
  Post([profile, limit, done = std::move(done)](State& state) mutable {
    std::vector<HistoryMatch> pages;
    if (HistoryStore* store = state.Store(profile)) {
      pages = store->RecentPages(limit);
    }
    CefPostTask(TID_UI, new FunctionTask([done = std::move(done),
                                          pages = std::move(pages)]() mutable {
                  done(std::move(pages));
                }));
  });
}

void HistoryService::Suggest(const ProfileId& profile,
                             std::string text,
                             std::size_t limit,
                             SuggestDone done) {
  CEF_REQUIRE_UI_THREAD();
  Post([profile, text = std::move(text), limit,
        done = std::move(done)](State& state) mutable {
    std::vector<HistoryMatch> matches;
    if (HistoryStore* store = state.Store(profile)) {
      matches = store->Suggest(text, Now(), limit);
    }
    CefPostTask(TID_UI,
                new FunctionTask([done = std::move(done),
                                  matches = std::move(matches)]() mutable {
                  done(std::move(matches));
                }));
  });
}

}  // namespace brouk
