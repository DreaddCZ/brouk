#ifndef BROUK_CEF_APP_LOGGING_H_
#define BROUK_CEF_APP_LOGGING_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "core/profile_registry.h"
#include "include/cef_app.h"

namespace brouk {

// Brouk's log (025), set up the same way on every platform.

// In the browser process, before CefInitialize: brouk.log in `logs`,
// debug logging as the settings say, and Chromium's own log
// (`logs`/chromium.log, the previous run's kept as chromium.1.log) only
// while debug logging is on and no other Brouk runs. Chromium reads its
// log setting only here, so turning debug logging on later adds Brouk's
// details at once and Chromium's at the next start.
void StartAppLogging(const std::filesystem::path& logs,
                     ProfileRegistry& profiles,
                     CefSettings& settings);

// After CefInitialize: log lines name CEF's threads ("UI", "IO"…).
void NameLogThreadsByCef();
// The name of the CEF thread the caller is on, or empty.
std::string CefThreadName();

// UI thread. Follows logging.* in the global settings: Apply() after every
// change switches debug logging on or off, writes its end time when it is
// switched on, and turns it off when that time comes (checked again then).
class DebugLoggingSwitch {
 public:
  explicit DebugLoggingSwitch(ProfileRegistry& profiles)
      : profiles_(profiles) {}
  DebugLoggingSwitch(const DebugLoggingSwitch&) = delete;
  DebugLoggingSwitch& operator=(const DebugLoggingSwitch&) = delete;

  void Apply();

 private:
  ProfileRegistry& profiles_;
  // Only the latest scheduled check runs.
  std::uint64_t generation_ = 0;
  // Expires with the switch, for checks that come later.
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace brouk

#endif  // BROUK_CEF_APP_LOGGING_H_
