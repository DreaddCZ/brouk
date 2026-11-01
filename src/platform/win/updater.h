#ifndef BROUK_PLATFORM_WIN_UPDATER_H_
#define BROUK_PLATFORM_WIN_UPDATER_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "core/update_feed.h"
#include "include/cef_urlrequest.h"

namespace brouk::win {

// Automatic updates on Windows (024, W4): Brouk's own small updater in the
// place of Sparkle (macOS, 019), as WinSparkle would bring wxWidgets. It
// reads the Windows feed of the releases repository about a minute after
// the start and then every 5 hours, downloads the newest release of the
// chosen channel, checks its Ed25519 signature against the key built into
// Brouk, and runs the installer silently when Brouk quits (or at once with
// "Restart to update"). The installer waits for Brouk to end (installer/
// brouk.iss). UI thread, except InstallAtExit.
class Updater {
 public:
  enum class State {
    kIdle,         // Nothing newer known.
    kChecking,     // Reading the feed.
    kAvailable,    // A newer release, not downloaded (updates.mode notify).
    kDownloading,  // Downloading or checking the signature.
    kReady,        // Downloaded and checked: installed at quit.
  };

  struct Config {
    std::string feed_url;
    std::string public_key;  // Ed25519, raw 32 bytes.
    std::string current_version;
    // Downloads go here; emptied at every start, as an installed update
    // leaves its installer behind.
    std::filesystem::path folder;
  };

  // `changed` runs whenever state() changes (to update the "⋮" menu).
  Updater(Config config, std::function<void()> changed);
  ~Updater();
  Updater(const Updater&) = delete;
  Updater& operator=(const Updater&) = delete;

  // The settings updates.channel and updates.mode (019): "install" checks
  // and downloads by itself, "notify" checks and asks, "off" checks only
  // when asked. A changed channel checks again.
  void SetSettings(const std::string& channel, const std::string& mode);

  // "Check for updates…": checks now; `done` gets an error for the user,
  // or "" when the check worked (state() then tells the result).
  using CheckDone = std::function<void(const std::string& error)>;
  void CheckNow(CheckDone done);
  // kAvailable: downloads the release ("Update Brouk to …").
  void Download();
  // Before CefShutdown: stops downloads and checks; a downloaded update
  // stays ready for InstallAtExit.
  void Stop();

  State state() const { return state_; }
  // The available or downloaded release's version, else "".
  std::string version() const;

  // After CefShutdown (no CEF calls): when an update is ready, starts its
  // installer, which waits for Brouk to end. `relaunch`: Brouk starts again
  // afterwards (Restart to update).
  void InstallAtExit(bool relaunch) const;

 private:
  class Fetch;

  void ScheduleCheck(std::int64_t delay_ms);
  void Check();
  void OnFeed(std::optional<std::string> body, const std::string& error);
  void OnDownloaded(bool ok, const std::string& error);
  void OnVerified(bool ok);
  void SetState(State state);
  void FinishCheck(const std::string& error);
  std::filesystem::path InstallerPath() const;

  const Config config_;
  std::function<void()> changed_;
  std::string channel_ = "stable";
  std::string mode_ = "install";
  bool started_ = false;
  State state_ = State::kIdle;
  std::optional<UpdateRelease> release_;
  CefRefPtr<Fetch> fetch_;
  CheckDone check_done_;
  // Each scheduled check carries the generation it was made in; a newer
  // schedule makes older ones do nothing.
  int schedule_generation_ = 0;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_UPDATER_H_
