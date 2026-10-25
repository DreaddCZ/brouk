#include "cef/download_service.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <utility>

#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

std::int64_t Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::int64_t Seconds(const CefBaseTime& time) {
  CefTime converted;
  if (time.val == 0 || !cef_time_from_basetime(time, &converted)) {
    return 0;
  }
  double seconds = 0;
  cef_time_to_doublet(&converted, &seconds);
  return static_cast<std::int64_t>(seconds);
}

// The live state of a CEF download item, kept in `record`.
void Read(const CefRefPtr<CefDownloadItem>& item, DownloadRecord& record) {
  record.url = item->GetURL().ToString();
  record.original_url = item->GetOriginalUrl().ToString();
  std::string path = item->GetFullPath().ToString();
  if (!path.empty()) {
    record.path = path;
  }
  record.mime_type = item->GetMimeType().ToString();
  record.received = item->GetReceivedBytes();
  record.total = std::max<std::int64_t>(item->GetTotalBytes(), 0);
  record.speed = item->GetCurrentSpeed();
  if (std::int64_t start = Seconds(item->GetStartTime()); start > 0) {
    record.start_time = start;
  }
  if (item->IsComplete()) {
    record.state = DownloadRecord::State::kComplete;
    record.end_time = Now();
  } else if (item->IsCanceled()) {
    record.state = DownloadRecord::State::kCancelled;
    record.end_time = Now();
  } else if (item->IsInterrupted()) {
    record.state = DownloadRecord::State::kFailed;
    record.error = DownloadInterruptText(item->GetInterruptReason());
    record.end_time = Now();
  } else if (item->IsPaused()) {
    record.state = DownloadRecord::State::kPaused;
  } else {
    record.state = DownloadRecord::State::kInProgress;
  }
}

}  // namespace

DownloadService::DownloadService(HistoryService* history)
    : history_(history), alive_(std::make_shared<bool>(true)) {}

void DownloadService::Changed() {
  if (changed_) {
    changed_();
  }
}

void DownloadService::OnStarted(const ProfileId& profile,
                                const CefRefPtr<CefDownloadItem>& item,
                                const std::string& referrer_site) {
  CEF_REQUIRE_UI_THREAD();
  std::uint32_t cef_id = item->GetId();
  if (session_.contains(cef_id)) {
    return;
  }
  Live live;
  live.profile = profile;
  Read(item, live.record);
  live.record.referrer_site = referrer_site;
  if (live.record.start_time == 0) {
    live.record.start_time = Now();
  }
  session_.emplace(cef_id, std::move(live));
  Save(cef_id);
  Changed();
}

void DownloadService::OnUpdated(
    const CefRefPtr<CefDownloadItem>& item,
    const CefRefPtr<CefDownloadItemCallback>& callback) {
  CEF_REQUIRE_UI_THREAD();
  auto it = session_.find(item->GetId());
  if (it == session_.end()) {
    return;  // Not started through a tab (should not happen).
  }
  Live& live = it->second;
  DownloadRecord::State before = live.record.state;
  Read(item, live.record);
  live.callback = live.record.Running() ? callback : nullptr;
  bool state_changed = before != live.record.state;
  // Progress is saved every few seconds; state changes at once.
  if (state_changed || Now() - live.last_saved >= 5) {
    Save(it->first);
  }
  if (state_changed && live.record.state == DownloadRecord::State::kComplete &&
      finished_) {
    finished_(live.record.path);
  }
  Changed();
}

void DownloadService::Save(std::uint32_t cef_id) {
  auto it = session_.find(cef_id);
  if (it == session_.end()) {
    return;
  }
  Live& live = it->second;
  live.last_saved = Now();
  if (live.record.id == 0 && live.save_pending) {
    return;  // The first save is still on its way; it saves the latest state.
  }
  bool first = live.record.id == 0;
  live.save_pending = first;
  auto record = std::make_shared<DownloadRecord>(live.record);
  auto id = std::make_shared<std::int64_t>(0);
  history_->Run(
      live.profile,
      [record, id](HistoryStore& store) {
        Result<std::int64_t> saved = store.SaveDownload(*record);
        if (saved) {
          *id = saved.value();
        }
      },
      [this, alive = std::weak_ptr<bool>(alive_), cef_id, id, first] {
        if (alive.expired()) {
          return;
        }
        auto entry = session_.find(cef_id);
        if (entry == session_.end() || !first || *id == 0) {
          return;
        }
        Live& saved = entry->second;
        saved.record.id = *id;
        saved.save_pending = false;
        // Updates that came in meanwhile.
        Save(cef_id);
      });
}

DownloadService::Live* DownloadService::Find(const ProfileId& profile,
                                             std::int64_t id) {
  for (auto& [cef_id, live] : session_) {
    if (live.profile == profile && live.record.id == id && id != 0) {
      return &live;
    }
  }
  return nullptr;
}

std::vector<DownloadEntry> DownloadService::Session() const {
  std::vector<DownloadEntry> entries;
  entries.reserve(session_.size());
  for (const auto& [cef_id, live] : session_) {
    entries.push_back({live.profile, live.record});
  }
  std::ranges::sort(entries, [](const auto& a, const auto& b) {
    return a.record.start_time > b.record.start_time;
  });
  return entries;
}

int DownloadService::RunningCount() const {
  return static_cast<int>(std::ranges::count_if(
      session_, [](const auto& pair) { return pair.second.record.Running(); }));
}

double DownloadService::Progress() const {
  std::int64_t received = 0;
  std::int64_t total = 0;
  for (const auto& [cef_id, live] : session_) {
    if (!live.record.Running()) {
      continue;
    }
    if (live.record.total <= 0) {
      return -1;
    }
    received += live.record.received;
    total += live.record.total;
  }
  return total > 0 ? static_cast<double>(received) / static_cast<double>(total)
                   : -1;
}

bool DownloadService::HasUnseen() const {
  return std::ranges::any_of(session_, [](const auto& pair) {
    return pair.second.record.state == DownloadRecord::State::kComplete &&
           !pair.second.record.opened;
  });
}

void DownloadService::MarkAllSeen() {
  CEF_REQUIRE_UI_THREAD();
  bool changed = false;
  for (auto& [cef_id, live] : session_) {
    if (live.record.state == DownloadRecord::State::kComplete &&
        !live.record.opened) {
      live.record.opened = true;
      Save(cef_id);
      changed = true;
    }
  }
  if (changed) {
    Changed();
  }
}

void DownloadService::List(const std::vector<ProfileId>& profiles,
                           std::size_t limit,
                           const ListDone& done) {
  CEF_REQUIRE_UI_THREAD();
  auto entries = std::make_shared<std::vector<DownloadEntry>>();
  auto remaining = std::make_shared<std::size_t>(profiles.size());
  auto finish = [this, alive = std::weak_ptr<bool>(alive_), entries, done] {
    if (alive.expired()) {
      return;
    }
    // This run's live state wins over the stored one.
    for (DownloadEntry& entry : *entries) {
      if (const Live* live = Find(entry.profile, entry.record.id)) {
        entry.record = live->record;
      }
    }
    std::ranges::sort(*entries, [](const auto& a, const auto& b) {
      return a.record.start_time > b.record.start_time ||
             (a.record.start_time == b.record.start_time &&
              a.record.id > b.record.id);
    });
    done(std::move(*entries));
  };
  if (profiles.empty()) {
    finish();
    return;
  }
  for (const ProfileId& profile : profiles) {
    auto found = std::make_shared<std::vector<DownloadRecord>>();
    history_->Run(
        profile,
        [found, limit](HistoryStore& store) {
          *found = store.Downloads(limit);
        },
        [entries, remaining, found, profile, finish] {
          for (DownloadRecord& record : *found) {
            entries->push_back({profile, std::move(record)});
          }
          if (--*remaining == 0) {
            finish();
          }
        });
  }
}

void DownloadService::Pause(const ProfileId& profile, std::int64_t id) {
  if (Live* live = Find(profile, id); live && live->callback) {
    live->callback->Pause();
  }
}

void DownloadService::Resume(const ProfileId& profile, std::int64_t id) {
  if (Live* live = Find(profile, id); live && live->callback) {
    live->callback->Resume();
  }
}

void DownloadService::Cancel(const ProfileId& profile, std::int64_t id) {
  if (Live* live = Find(profile, id); live && live->callback) {
    live->callback->Cancel();
  }
}

void DownloadService::Retry(const ProfileId& /*profile*/,
                            std::int64_t /*id*/,
                            const std::string& url,
                            const CefRefPtr<CefBrowser>& browser) {
  if (browser && !url.empty()) {
    browser->GetHost()->StartDownload(url);
  }
}

void DownloadService::Remove(const ProfileId& profile, std::int64_t id) {
  CEF_REQUIRE_UI_THREAD();
  for (auto it = session_.begin(); it != session_.end(); ++it) {
    if (it->second.profile == profile && it->second.record.id == id) {
      if (it->second.callback) {
        it->second.callback->Cancel();
      }
      session_.erase(it);
      break;
    }
  }
  history_->Run(
      profile, [id](HistoryStore& store) { store.RemoveDownload(id); },
      [this, alive = std::weak_ptr<bool>(alive_)] {
        if (!alive.expired()) {
          Changed();
        }
      });
}

void DownloadService::ClearAll(const std::vector<ProfileId>& profiles,
                               std::function<void()> done) {
  CEF_REQUIRE_UI_THREAD();
  std::erase_if(session_, [&](const auto& pair) {
    return !pair.second.record.Running() &&
           std::ranges::find(profiles, pair.second.profile) != profiles.end();
  });
  auto remaining = std::make_shared<std::size_t>(profiles.size());
  auto shared_done = std::make_shared<std::function<void()>>(std::move(done));
  if (profiles.empty()) {
    (*shared_done)();
    return;
  }
  for (const ProfileId& profile : profiles) {
    history_->Run(
        profile, [](HistoryStore& store) { store.DeleteDownloadsSince(0); },
        [this, alive = std::weak_ptr<bool>(alive_), remaining, shared_done] {
          if (--*remaining == 0) {
            if (!alive.expired()) {
              Changed();
            }
            (*shared_done)();
          }
        });
  }
}

}  // namespace brouk
