#include "cef/adblock_service.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_request_context.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr std::string_view kResourcesId = "resources";
constexpr char kResourcesUrl[] =
    "https://raw.githubusercontent.com/brave/adblock-resources/master/dist/"
    "resources.json";
// The first check waits a little after start (startup stays quick), then
// lists are checked hourly; each is downloaded when due (007).
constexpr int64_t kFirstCheckMs = 15 * 1000;
constexpr int64_t kCheckEveryMs = 60 * 60 * 1000;

class FunctionTask : public CefTask {
 public:
  explicit FunctionTask(std::function<void()> function)
      : function_(std::move(function)) {}
  void Execute() override { function_(); }

 private:
  std::function<void()> function_;
  IMPLEMENT_REFCOUNTING(FunctionTask);
};

std::int64_t Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

struct AdblockService::State {
  std::unique_ptr<AdblockStore> store;
  std::string bundled_resources;
  std::shared_ptr<Engines> engines;
  std::vector<FilterListDef> lists;
  std::vector<std::set<FilterCategory>> category_sets;

  // Builds (or loads from the cache) an engine for each category set and
  // publishes them together.
  void Rebuild() {
    std::string resources = store->ReadResources().value_or(bundled_resources);
    std::map<std::string, std::shared_ptr<const ActiveAdblockEngine>> built;
    for (const std::set<FilterCategory>& categories : category_sets) {
      std::string key = FormatFilterCategories(categories);
      if (built.contains(key)) {
        continue;
      }
      std::vector<FilterListDef> chosen;
      for (const FilterListDef& list : lists) {
        if (categories.contains(list.category)) {
          chosen.push_back(list);
        }
      }
      BuiltEngine engine = store->LoadOrBuildEngine(chosen, resources);
      if (engine.engine) {
        auto active = std::make_shared<ActiveAdblockEngine>();
        active->engine = std::move(engine.engine);
        active->list_ids = std::move(engine.list_ids);
        built[key] = std::move(active);
      }
    }
    std::lock_guard<std::mutex> lock(engines->mutex);
    engines->by_key = std::move(built);
  }
};

// Downloads one list. UI thread.
class AdblockService::Download : public CefURLRequestClient {
 public:
  using Done =
      std::function<void(std::optional<std::string> body, std::string error)>;

  static CefRefPtr<Download> Start(const std::string& url, Done done) {
    CEF_REQUIRE_UI_THREAD();
    CefRefPtr<CefRequest> request = CefRequest::Create();
    request->SetURL(url);
    request->SetMethod("GET");
    // Lists are public files: no cookies or credentials are sent.
    request->SetFlags(UR_FLAG_DISABLE_CACHE);
    CefRefPtr<Download> client(new Download(std::move(done)));
    client->request_ = CefURLRequest::Create(
        request, client, CefRequestContext::GetGlobalContext());
    return client;
  }

  void Cancel() {
    done_ = nullptr;
    if (request_) {
      request_->Cancel();
    }
  }

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    CEF_REQUIRE_UI_THREAD();
    if (!done_) {
      return;
    }
    Done done = std::move(done_);
    done_ = nullptr;
    CefRefPtr<CefResponse> response = request->GetResponse();
    int status = response ? response->GetStatus() : 0;
    if (too_big_) {
      done(std::nullopt, "The list is too large.");
    } else if (request->GetRequestStatus() != UR_SUCCESS) {
      done(std::nullopt, "The list could not be downloaded (network error " +
                             std::to_string(request->GetRequestError()) + ").");
    } else if (status != 200) {
      done(std::nullopt, "The server answered " + std::to_string(status) +
                             " instead of the list.");
    } else {
      done(std::move(body_), "");
    }
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest> /*request*/,
                        int64_t /*current*/,
                        int64_t /*total*/) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> /*request*/,
                          int64_t /*current*/,
                          int64_t /*total*/) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request,
                      const void* data,
                      size_t data_length) override {
    CEF_REQUIRE_UI_THREAD();
    if (body_.size() + data_length > kMaxFilterListBytes) {
      too_big_ = true;
      body_.clear();
      request->Cancel();
      return;
    }
    body_.append(static_cast<const char*>(data), data_length);
  }
  bool GetAuthCredentials(bool /*isProxy*/,
                          const CefString& /*host*/,
                          int /*port*/,
                          const CefString& /*realm*/,
                          const CefString& /*scheme*/,
                          CefRefPtr<CefAuthCallback> /*callback*/) override {
    return false;
  }

 private:
  explicit Download(Done done) : done_(std::move(done)) {}

  Done done_;
  CefRefPtr<CefURLRequest> request_;
  std::string body_;
  bool too_big_ = false;
  IMPLEMENT_REFCOUNTING(Download);
};

AdblockService::AdblockService(std::filesystem::path dir,
                               std::string bundled_resources)
    : thread_(CefThread::CreateThread("BroukAdblock")),
      state_(std::make_shared<State>()),
      engines_(std::make_shared<Engines>()),
      alive_(std::make_shared<bool>(true)) {
  CEF_REQUIRE_UI_THREAD();
  state_->bundled_resources = std::move(bundled_resources);
  state_->engines = engines_;
  Post([dir = std::move(dir)](State& state) {
    state.store = std::make_unique<AdblockStore>(dir);
  });
}

AdblockService::~AdblockService() {
  CEF_REQUIRE_UI_THREAD();
  *alive_ = false;
  if (download_) {
    download_->Cancel();
  }
  thread_->Stop();
}

void AdblockService::Post(std::function<void(State&)> work) {
  std::shared_ptr<State> state = state_;
  thread_->GetTaskRunner()->PostTask(
      new FunctionTask([state, work = std::move(work)] { work(*state); }));
}

void AdblockService::SetChangedCallback(std::function<void()> changed) {
  CEF_REQUIRE_UI_THREAD();
  changed_ = std::move(changed);
}

void AdblockService::Configure(
    std::vector<FilterListDef> lists,
    std::vector<std::set<FilterCategory>> category_sets) {
  CEF_REQUIRE_UI_THREAD();
  lists_ = lists;
  std::shared_ptr<bool> alive = alive_;
  Post([this, alive, lists = std::move(lists),
        sets = std::move(category_sets)](State& state) {
    state.lists = lists;
    state.category_sets = sets;
    std::vector<std::string> ids;
    for (const FilterListDef& list : lists) {
      ids.push_back(list.id);
    }
    state.store->RemoveUnused(ids);
    state.Rebuild();
    auto states = state.store->states();
    CefPostTask(TID_UI, new FunctionTask([this, alive, states] {
                  if (*alive) {
                    states_ = states;
                    if (changed_) {
                      changed_();
                    }
                  }
                }));
  });
  ScheduleCheck(kFirstCheckMs);
}

std::shared_ptr<const ActiveAdblockEngine> AdblockService::EngineFor(
    const std::set<FilterCategory>& categories) const {
  std::string key = FormatFilterCategories(categories);
  std::lock_guard<std::mutex> lock(engines_->mutex);
  auto it = engines_->by_key.find(key);
  return it == engines_->by_key.end() ? nullptr : it->second;
}

void AdblockService::ScheduleCheck(int64_t delay_ms) {
  std::shared_ptr<bool> alive = alive_;
  CefPostDelayedTask(TID_UI, new FunctionTask([this, alive] {
                       if (*alive) {
                         CheckForUpdates(false);
                       }
                     }),
                     delay_ms);
}

void AdblockService::UpdateNow() {
  CEF_REQUIRE_UI_THREAD();
  CheckForUpdates(true);
}

void AdblockService::CheckForUpdates(bool everything) {
  CEF_REQUIRE_UI_THREAD();
  if (updating_) {
    return;
  }
  updating_ = true;
  if (changed_) {
    changed_();
  }
  std::shared_ptr<bool> alive = alive_;
  std::vector<FilterListDef> candidates = lists_;
  candidates.push_back({.id = std::string(kResourcesId),
                        .name = "Resources",
                        .category = FilterCategory::kCustom,
                        .url = kResourcesUrl,
                        .hosts_format = false,
                        .trusted = false,
                        .default_on = true,
                        .issues_url = ""});
  Post([this, alive, everything,
        candidates = std::move(candidates)](State& state) {
    std::vector<FilterListDef> due;
    std::int64_t now = Now();
    for (const FilterListDef& list : candidates) {
      if (everything || state.store->IsDue(list.id, now)) {
        due.push_back(list);
      }
    }
    CefPostTask(TID_UI, new FunctionTask([this, alive, due] {
                  if (!*alive) {
                    return;
                  }
                  queue_ = due;
                  downloaded_any_ = false;
                  DownloadNext();
                }));
  });
}

void AdblockService::DownloadNext() {
  CEF_REQUIRE_UI_THREAD();
  if (queue_.empty()) {
    download_ = nullptr;
    Finished(downloaded_any_);
    return;
  }
  FilterListDef list = queue_.front();
  queue_.erase(queue_.begin());
  std::shared_ptr<bool> alive = alive_;
  download_ = Download::Start(
      list.url, [this, alive, id = list.id](std::optional<std::string> body,
                                            std::string error) {
        if (!*alive) {
          return;
        }
        Post([this, alive, id, body = std::move(body),
              error = std::move(error)](State& state) {
          std::int64_t now = Now();
          bool ok = false;
          if (!body) {
            state.store->SaveError(id, error, now);
          } else if (id == kResourcesId) {
            ok = !state.store->SaveResources(*body, now);
          } else {
            ok = !state.store->SaveDownload(id, *body, now);
          }
          auto states = state.store->states();
          CefPostTask(TID_UI, new FunctionTask([this, alive, ok, states] {
                        if (!*alive) {
                          return;
                        }
                        downloaded_any_ = downloaded_any_ || ok;
                        states_ = states;
                        DownloadNext();
                      }));
        });
      });
}

void AdblockService::Finished(bool rebuild) {
  CEF_REQUIRE_UI_THREAD();
  std::shared_ptr<bool> alive = alive_;
  auto done = [this, alive] {
    if (!*alive) {
      return;
    }
    updating_ = false;
    if (changed_) {
      changed_();
    }
  };
  if (rebuild) {
    Post([done](State& state) {
      state.Rebuild();
      CefPostTask(TID_UI, new FunctionTask(done));
    });
  } else {
    done();
  }
  ScheduleCheck(kCheckEveryMs);
}

void AdblockService::ReadMyFilters(std::function<void(std::string)> done) {
  CEF_REQUIRE_UI_THREAD();
  Post([done = std::move(done)](State& state) {
    std::string text = state.store->ReadMyFilters();
    CefPostTask(TID_UI, new FunctionTask([done, text] { done(text); }));
  });
}

void AdblockService::SaveMyFilters(std::string text,
                                   std::function<void(bool)> done) {
  CEF_REQUIRE_UI_THREAD();
  std::shared_ptr<bool> alive = alive_;
  Post([this, alive, text = std::move(text),
        done = std::move(done)](State& state) {
    bool saved = !state.store->SaveMyFilters(text);
    if (saved) {
      state.Rebuild();
    }
    CefPostTask(TID_UI, new FunctionTask([this, alive, done, saved] {
                  if (*alive && changed_) {
                    changed_();
                  }
                  done(saved);
                }));
  });
}

}  // namespace brouk
