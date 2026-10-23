#ifndef BROUK_CORE_SERVICE_WORKERS_H_
#define BROUK_CORE_SERVICE_WORKERS_H_

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace brouk {

// A service worker registration as the protocol's ServiceWorker events
// report it (006, Site data). Everything comes from the page's renderer
// and is only shown.
struct ServiceWorkerRegistration {
  std::string registration_id;
  std::string scope_url;
  bool is_deleted = false;
};

struct ServiceWorkerVersion {
  std::string version_id;
  std::string registration_id;
  std::string script_url;
  std::string running_status;  // "stopped", "starting", "running", ...
  std::string status;          // "new", "installing", ..., "redundant"
};

// One registration with its current worker, for the Site data tool.
struct ServiceWorkerEntry {
  std::string registration_id;
  std::string scope_url;
  std::string script_url;      // "" while no version is known.
  std::string running_status;  // Of the current version.
  std::string status;
};

// The service workers known to one tab's protocol session, kept up to
// date from ServiceWorker.workerRegistrationUpdated and
// workerVersionUpdated events. Bounded, as the events come from the web.
class ServiceWorkerList {
 public:
  static constexpr std::size_t kMaxEntries = 500;

  void OnRegistrationUpdated(const ServiceWorkerRegistration& registration);
  void OnVersionUpdated(const ServiceWorkerVersion& version);
  void Clear();

  // Registrations that are not deleted, by scope, each with its current
  // version: the newest one that is not redundant (an activated one wins).
  std::vector<ServiceWorkerEntry> Entries() const;

 private:
  std::map<std::string, ServiceWorkerRegistration> registrations_;
  std::map<std::string, ServiceWorkerVersion> versions_;
};

}  // namespace brouk

#endif  // BROUK_CORE_SERVICE_WORKERS_H_
