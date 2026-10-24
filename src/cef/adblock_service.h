#ifndef BROUK_CEF_ADBLOCK_SERVICE_H_
#define BROUK_CEF_ADBLOCK_SERVICE_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "core/adblock_engine.h"
#include "core/adblock_lists.h"
#include "core/adblock_policy.h"
#include "core/adblock_store.h"
#include "core/profile_id.h"
#include "include/cef_thread.h"
#include "include/cef_urlrequest.h"

namespace brouk {

// An engine in use, with the ids of the lists it was built from (index =
// list number in matches).
struct ActiveAdblockEngine {
  std::unique_ptr<AdblockEngine> engine;
  std::vector<std::string> list_ids;
};

// The ad blocker (007): downloads the subscribed lists and keeps them up to
// date, and builds one engine per set of categories the profiles use.
// Files and engine builds run on the ad blocker's own thread; the methods
// are for the UI thread unless they say otherwise.
class AdblockService {
 public:
  // `dir` is adblock/ in the app data folder; `bundled_resources` the
  // redirect and scriptlet resources shipped with the app (used until a
  // newer copy is downloaded).
  AdblockService(std::filesystem::path dir, std::string bundled_resources);
  ~AdblockService();
  AdblockService(const AdblockService&) = delete;
  AdblockService& operator=(const AdblockService&) = delete;

  // The lists in use and the category sets of the profiles. Engines are
  // rebuilt (from the cache when possible) and due lists downloaded.
  void Configure(std::vector<FilterListDef> lists,
                 std::vector<std::set<FilterCategory>> category_sets);

  // The engine for a profile's categories; null until its lists are there.
  // Any thread (requests are checked on the IO thread).
  std::shared_ptr<const ActiveAdblockEngine> EngineFor(
      const std::set<FilterCategory>& categories) const;

  // How each profile blocks, and hosts never blocked by broad rules (the
  // hosts of routing rules, 010). UI thread.
  void SetPolicies(std::map<ProfileId, ProfileAdblockPolicy> policies,
                   std::vector<std::string> protected_hosts);
  // Any thread. Null for a profile without a policy (not blocking).
  std::shared_ptr<const ProfileAdblockPolicy> PolicyFor(
      const ProfileId& profile) const;
  std::shared_ptr<const std::vector<std::string>> ProtectedHosts() const;
  // Which list an id is, for messages ("EasyList"). Any thread.
  std::string ListName(const std::string& id) const;

  // Downloads every list now ("Update now" in settings).
  void UpdateNow();
  bool updating() const { return updating_; }

  // The lists' download state as last known on the UI thread.
  const std::map<std::string, FilterListState, std::less<>>& list_states()
      const {
    return states_;
  }
  // Called on the UI thread when list states or engines changed.
  void SetChangedCallback(std::function<void()> changed);

  // My filters (007): read, and replace (rebuilding the engines).
  void ReadMyFilters(std::function<void(std::string)> done);
  void SaveMyFilters(std::string text, std::function<void(bool saved)> done);

 private:
  struct State;
  class Download;

  void Post(std::function<void(State&)> work);
  void CheckForUpdates(bool everything);
  void DownloadNext();
  void Finished(bool rebuild);
  void ScheduleCheck(int64_t delay_ms);

  CefRefPtr<CefThread> thread_;
  std::shared_ptr<State> state_;  // Used on the ad blocker's thread.

  // Engines by category key: published by the ad blocker's thread, read on
  // any thread.
  struct Engines {
    mutable std::mutex mutex;  // Guards by_key.
    std::map<std::string, std::shared_ptr<const ActiveAdblockEngine>> by_key;
  };
  std::shared_ptr<Engines> engines_;

  // Read on any thread.
  mutable std::mutex policies_mutex_;  // Guards the three below.
  std::map<ProfileId, std::shared_ptr<const ProfileAdblockPolicy>> policies_;
  std::shared_ptr<const std::vector<std::string>> protected_hosts_ =
      std::make_shared<const std::vector<std::string>>();
  std::map<std::string, std::string> list_names_;

  // UI thread.
  std::vector<FilterListDef> lists_;
  std::map<std::string, FilterListState, std::less<>> states_;
  std::vector<FilterListDef> queue_;  // Lists waiting for download.
  CefRefPtr<Download> download_;
  bool updating_ = false;
  bool downloaded_any_ = false;
  std::function<void()> changed_;
  std::shared_ptr<bool> alive_;  // For delayed tasks after destruction.
};

}  // namespace brouk

#endif  // BROUK_CEF_ADBLOCK_SERVICE_H_
