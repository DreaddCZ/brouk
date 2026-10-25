#ifndef BROUK_CEF_DOWNLOAD_SERVICE_H_
#define BROUK_CEF_DOWNLOAD_SERVICE_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cef/history_service.h"
#include "core/download_record.h"
#include "core/profile_id.h"
#include "include/cef_browser.h"
#include "include/cef_download_handler.h"
#include "include/cef_download_item.h"

namespace brouk {

// A download with the profile it belongs to.
struct DownloadEntry {
  ProfileId profile;
  DownloadRecord record;
};

// All downloads of the app (014), whichever tab or profile started them:
// the running ones of this run in memory (with CEF's callbacks for pause,
// resume and cancel), every record in its profile's history database. The
// toolbar button, the popup, the downloads page and the Dock follow it. UI
// thread only.
class DownloadService {
 public:
  // `history` outlives the service.
  explicit DownloadService(HistoryService* history);
  DownloadService(const DownloadService&) = delete;
  DownloadService& operator=(const DownloadService&) = delete;

  // From a tab (TabClient): a download got its target; its updates.
  void OnStarted(const ProfileId& profile,
                 const CefRefPtr<CefDownloadItem>& item,
                 const std::string& referrer_site);
  void OnUpdated(const CefRefPtr<CefDownloadItem>& item,
                 const CefRefPtr<CefDownloadItemCallback>& callback);

  // The downloads of this run (running and finished), newest first.
  std::vector<DownloadEntry> Session() const;
  // Whether any download started in this run (the toolbar button appears).
  bool AnyThisRun() const { return !session_.empty(); }
  int RunningCount() const;
  // Overall progress of the running downloads, 0..1; -1 if unknown.
  double Progress() const;
  // A download finished that the user has not looked at.
  bool HasUnseen() const;
  // The popup was opened: finished downloads count as seen.
  void MarkAllSeen();

  // The lists of the popup and the page: the given profiles' records
  // (newest first, at most `limit` each), with this run's live state.
  using ListDone = std::function<void(std::vector<DownloadEntry>)>;
  void List(const std::vector<ProfileId>& profiles,
            std::size_t limit,
            const ListDone& done);

  // Actions on one download (a record id of a profile).
  void Pause(const ProfileId& profile, std::int64_t id);
  void Resume(const ProfileId& profile, std::int64_t id);
  void Cancel(const ProfileId& profile, std::int64_t id);
  // Starts it again in `browser` (a tab of its profile).
  void Retry(const ProfileId& profile,
             std::int64_t id,
             const std::string& url,
             const CefRefPtr<CefBrowser>& browser);
  // Removes it from the list (not the file); running ones are cancelled.
  void Remove(const ProfileId& profile, std::int64_t id);
  // Clear all (014): every finished record of `profiles`.
  void ClearAll(const std::vector<ProfileId>& profiles,
                std::function<void()> done);

  // Called after every change, and when a download finished (with its
  // path) so the Dock can bounce the Downloads stack.
  void SetChangedCallback(std::function<void()> changed) {
    changed_ = std::move(changed);
  }
  void SetFinishedCallback(std::function<void(const std::string&)> finished) {
    finished_ = std::move(finished);
  }

 private:
  struct Live {
    ProfileId profile;
    DownloadRecord record;  // id 0 until the database gave one.
    CefRefPtr<CefDownloadItemCallback> callback;
    std::int64_t last_saved = 0;
    bool save_pending = false;  // An update arrived before the id.
  };
  Live* Find(const ProfileId& profile, std::int64_t id);
  void Save(std::uint32_t cef_id);
  void Changed();

  HistoryService* history_;
  // Callbacks from the history thread check it: the service may be gone.
  std::shared_ptr<bool> alive_;
  std::map<std::uint32_t, Live> session_;  // By CEF's id (this run only).
  std::function<void()> changed_;
  std::function<void(const std::string&)> finished_;
};

}  // namespace brouk

#endif  // BROUK_CEF_DOWNLOAD_SERVICE_H_
