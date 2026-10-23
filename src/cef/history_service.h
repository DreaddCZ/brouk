#ifndef BROUK_CEF_HISTORY_SERVICE_H_
#define BROUK_CEF_HISTORY_SERVICE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/history_store.h"
#include "core/profile_id.h"
#include "include/cef_thread.h"

namespace brouk {

// The profiles' history databases (013), used from the UI thread. All
// database work runs on one dedicated thread, so the UI never waits for the
// disk and each database is only ever touched by that thread. Results come
// back on the UI thread.
class HistoryService {
 public:
  // `database_path` gives a profile's history.db; called on the history
  // thread. UI thread.
  explicit HistoryService(
      std::function<std::filesystem::path(const ProfileId&)> database_path);
  // Waits for queued work and closes the databases. UI thread.
  ~HistoryService();
  HistoryService(const HistoryService&) = delete;
  HistoryService& operator=(const HistoryService&) = delete;

  // How long visits are kept (the setting history.keep): days, or nullopt
  // for forever. Applied when a database opens and then about hourly.
  void SetRetentionDays(std::optional<int> days);

  void RecordVisit(const ProfileId& profile,
                   std::string url,
                   std::string title,
                   VisitTransition transition);
  void SetTitle(const ProfileId& profile, std::string url, std::string title);
  void DeletePage(const ProfileId& profile, std::string url);
  // Closes a profile's database before its folder is deleted (005).
  void CloseProfile(const ProfileId& profile);

  using SuggestDone = std::function<void(std::vector<HistoryMatch>)>;
  // The most recently visited pages of `profile` (History → Recently
  // Visited); `done` runs on the UI thread.
  void RecentPages(const ProfileId& profile,
                   std::size_t limit,
                   SuggestDone done);
  // Address bar suggestions from `profile`'s history; `done` runs on the UI
  // thread.
  void Suggest(const ProfileId& profile,
               std::string text,
               std::size_t limit,
               SuggestDone done);

 private:
  // Only used on the history thread.
  struct State;

  void Post(std::function<void(State&)> work);

  CefRefPtr<CefThread> thread_;
  std::shared_ptr<State> state_;
};

}  // namespace brouk

#endif  // BROUK_CEF_HISTORY_SERVICE_H_
