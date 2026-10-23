#include "core/service_workers.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace brouk {

namespace {

// Version ids are numbers as strings; newer versions have larger ids.
bool IsNewer(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) {
    return a.size() > b.size();
  }
  return a > b;
}

}  // namespace

void ServiceWorkerList::OnRegistrationUpdated(
    const ServiceWorkerRegistration& registration) {
  if (registration.registration_id.empty()) {
    return;
  }
  if (!registrations_.contains(registration.registration_id) &&
      registrations_.size() >= kMaxEntries) {
    return;
  }
  registrations_[registration.registration_id] = registration;
}

void ServiceWorkerList::OnVersionUpdated(const ServiceWorkerVersion& version) {
  if (version.version_id.empty()) {
    return;
  }
  if (!versions_.contains(version.version_id) &&
      versions_.size() >= kMaxEntries) {
    // Drop redundant versions first; they are never shown.
    std::erase_if(versions_, [](const auto& item) {
      return item.second.status == "redundant";
    });
    if (versions_.size() >= kMaxEntries) {
      return;
    }
  }
  versions_[version.version_id] = version;
}

void ServiceWorkerList::Clear() {
  registrations_.clear();
  versions_.clear();
}

std::vector<ServiceWorkerEntry> ServiceWorkerList::Entries() const {
  std::vector<ServiceWorkerEntry> entries;
  for (const auto& [id, registration] : registrations_) {
    if (registration.is_deleted) {
      continue;
    }
    const ServiceWorkerVersion* current = nullptr;
    for (const auto& [version_id, version] : versions_) {
      if (version.registration_id != id || version.status == "redundant") {
        continue;
      }
      bool activated = version.status == "activated";
      bool current_activated = current && current->status == "activated";
      if (!current || (activated && !current_activated) ||
          (activated == current_activated &&
           IsNewer(version_id, current->version_id))) {
        current = &version;
      }
    }
    ServiceWorkerEntry entry;
    entry.registration_id = id;
    entry.scope_url = registration.scope_url;
    if (current) {
      entry.script_url = current->script_url;
      entry.running_status = current->running_status;
      entry.status = current->status;
    }
    entries.push_back(std::move(entry));
  }
  std::ranges::sort(entries, {}, &ServiceWorkerEntry::scope_url);
  return entries;
}

}  // namespace brouk
