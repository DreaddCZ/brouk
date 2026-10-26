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

#include "core/history_sql.h"
#include "core/history_store.h"
#include "core/profile_id.h"
#include "core/url_pattern.h"
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
  // Sites never recorded (history.never_record); their pages already in the
  // history are removed, in every profile as its history opens.
  void SetNeverRecord(std::vector<UrlPattern> patterns);

  void RecordVisit(const ProfileId& profile,
                   std::string url,
                   std::string title,
                   VisitTransition transition);
  void SetTitle(const ProfileId& profile, std::string url, std::string title);
  void DeletePage(const ProfileId& profile, std::string url);
  // The page forwarded to another page by itself (a client redirect): its
  // latest visit is removed, as only the end of a redirect chain is kept.
  void RemoveRedirectSource(const ProfileId& profile, std::string url);
  // A page's favicon (013), as the tab downloaded it (PNG). Not stored for
  // pages that are not recorded.
  void SetFavicon(const ProfileId& profile,
                  std::string page_url,
                  std::string icon_url,
                  std::vector<std::uint8_t> png);
  // The bookmarked pages (011), whose icons stay when they leave the
  // history; applied to every profile.
  void SetKeptIconPages(std::vector<std::string> urls);
  using IconsDone =
      std::function<void(std::map<std::string, std::vector<std::uint8_t>>)>;
  // The icons of `urls` in `profile`'s history (the page's own, else its
  // host's), by URL; pages without one are left out. `done` runs on the UI
  // thread.
  void Favicons(const ProfileId& profile,
                std::vector<std::string> urls,
                IconsDone done);
  // Closes a profile's database before its folder is deleted (005).
  void CloseProfile(const ProfileId& profile);

  // Runs `work` with `profile`'s history on the history thread (not if it
  // cannot be opened), then `done` on the UI thread. `work` must not keep
  // the store; results go through state both functions share.
  void Run(const ProfileId& profile,
           std::function<void(HistoryStore&)> work,
           std::function<void()> done);

  // The history page's SQL editor (013): runs `sql` over the histories of
  // `profiles` (id and name) on the history thread; `done` on the UI thread.
  using SqlDone = std::function<void(Result<SqlResult>)>;
  void RunSql(std::vector<std::pair<ProfileId, std::string>> profiles,
              std::string sql,
              SqlDone done);

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
