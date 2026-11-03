#include "cef/app_logging.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <system_error>
#include <utility>

#include "cef/function_task.h"
#include "core/debug_logging.h"
#include "core/log.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// Chromium's log does not rotate itself: at each start with debug logging
// on, a log over this size becomes chromium.1.log (one older file kept).
constexpr std::uintmax_t kChromiumLogMaxBytes = std::uintmax_t{5} * 1024 * 1024;
// Checked at least this often, so a computer that slept still turns debug
// logging off soon after its time.
constexpr auto kLongestCheckDelay = std::chrono::hours(1);

// Reads logging.* and writes what the decision asks for.
DebugLoggingDecision Decide(ProfileRegistry& profiles) {
  const Settings& global = profiles.global_settings();
  const DebugLoggingDecision decision =
      DecideDebugLogging({.debug = global.GetBool("logging.debug"),
                          .until = global.GetString("logging.debug_until"),
                          .hours = global.GetInt("logging.debug_hours")},
                         std::chrono::system_clock::now());
  if (decision.write_until) {
    if (std::optional<Error> error = profiles.SetGlobalSetting(
            "logging.debug_until", SettingValue(*decision.write_until))) {
      LogWarning() << "Logging: " << error->message;
    }
  }
  if (decision.write_debug) {
    if (std::optional<Error> error = profiles.SetGlobalSetting(
            "logging.debug", SettingValue(*decision.write_debug))) {
      LogWarning() << "Logging: " << error->message;
    }
  }
  if (decision.on != DebugLoggingOn()) {
    // Written while on, so both switching on and off show in the log.
    SetDebugLogging(true);
    LogInfo() << "Logging: debug logging "
              << (decision.on ? "switched on" : "switched off");
    SetDebugLogging(decision.on);
  }
  return decision;
}

}  // namespace

void NameLogThreadsByCef() {
  SetLogThreadNamer(&CefThreadName);
}

std::string CefThreadName() {
  constexpr std::array<std::pair<cef_thread_id_t, const char*>, 7> kThreads = {{
      {TID_UI, "UI"},
      {TID_IO, "IO"},
      {TID_FILE_BACKGROUND, "FILE_BACKGROUND"},
      {TID_FILE_USER_VISIBLE, "FILE_USER_VISIBLE"},
      {TID_FILE_USER_BLOCKING, "FILE_USER_BLOCKING"},
      {TID_PROCESS_LAUNCHER, "PROCESS_LAUNCHER"},
      {TID_RENDERER, "RENDERER"},
  }};
  for (const auto& [id, name] : kThreads) {
    if (CefCurrentlyOn(id)) {
      return name;
    }
  }
  return {};
}

void StartAppLogging(const std::filesystem::path& logs,
                     ProfileRegistry& profiles,
                     CefSettings& settings) {
  if (std::optional<Error> error = StartLogFile(logs)) {
    LogWarning() << "Logging: " << error->message;
  }
  const DebugLoggingDecision decision = Decide(profiles);
  if (!decision.on) {
    // No file; FATAL messages still go to stderr.
    settings.log_severity = LOGSEVERITY_DISABLE;
    return;
  }
  const std::filesystem::path chromium = logs / "chromium.log";
  std::error_code error;
  if (std::filesystem::file_size(chromium, error) > kChromiumLogMaxBytes &&
      !error) {
    std::filesystem::rename(chromium, logs / "chromium.1.log", error);
  }
  CefString(&settings.log_file) = chromium.native();
  settings.log_severity = LOGSEVERITY_INFO;
  settings.log_items = static_cast<cef_log_items_t>(LOG_ITEMS_FLAG_PROCESS_ID |
                                                    LOG_ITEMS_FLAG_THREAD_ID |
                                                    LOG_ITEMS_FLAG_TIME_STAMP);
}

void DebugLoggingSwitch::Apply() {
  CEF_REQUIRE_UI_THREAD();
  const DebugLoggingDecision decision = Decide(profiles_);
  const std::uint64_t generation = ++generation_;
  if (!decision.recheck_in) {
    return;
  }
  const auto delay = std::min<std::chrono::system_clock::duration>(
      *decision.recheck_in + std::chrono::seconds(1), kLongestCheckDelay);
  std::weak_ptr<int> alive = alive_;
  CefPostDelayedTask(
      TID_UI, new FunctionTask([this, alive, generation] {
        if (!alive.expired() && generation == generation_) {
          Apply();
        }
      }),
      std::chrono::duration_cast<std::chrono::milliseconds>(delay).count());
}

}  // namespace brouk
