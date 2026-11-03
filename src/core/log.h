#ifndef BROUK_CORE_LOG_H_
#define BROUK_CORE_LOG_H_

#include <chrono>
#include <filesystem>
#include <optional>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>

#include "core/log_file.h"
#include "core/result.h"

namespace brouk {

// Brouk's log (025). Warnings and errors always go to the log file and to
// stderr; debug and info messages only while debug logging is on, so they
// cost nothing otherwise. Usable from any thread.
//
//   LogWarning() << "Updates: download failed: " << error;
//   LogDebug() << "Passwords: save offered for " << UrlForLog(url);
//
// Never log passwords, master passwords, keys, tokens, cookies, form
// contents, autofill values or request bodies, and log URLs only through
// UrlForLog: help promises the log never contains them.
enum class LogLevel { kDebug, kInfo, kWarning, kError };

// One message, written when it goes out of scope (at the end of the
// statement).
class LogMessage {
 public:
  LogMessage(LogLevel level, std::source_location where);
  ~LogMessage();
  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(const LogMessage&) = delete;

  template <typename T>
  LogMessage& operator<<(const T& value) {
    if (enabled_) {
      stream_ << value;
    }
    return *this;
  }

 private:
  LogLevel level_;
  std::source_location where_;
  bool enabled_;
  std::ostringstream stream_;
};

LogMessage LogDebug(
    std::source_location where = std::source_location::current());
LogMessage LogInfo(
    std::source_location where = std::source_location::current());
LogMessage LogWarning(
    std::source_location where = std::source_location::current());
LogMessage LogError(
    std::source_location where = std::source_location::current());

// Starts writing to `directory`/brouk.log, rotated (3 files of 5 MB unless
// `limits` say otherwise). Until then, and if it fails, messages go to
// stderr only.
std::optional<Error> StartLogFile(const std::filesystem::path& directory,
                                  RotatingLogFile::Limits limits = {});
// Stops writing to the file (tests, and quitting).
void StopLogFile();

// Debug logging (the setting logging.debug): adds debug and info messages.
void SetDebugLogging(bool on);
bool DebugLoggingOn();
// Whether messages are also written to stderr (default: yes; tests turn it
// off).
void SetLogToStderr(bool on);
// How log lines name the calling thread ("UI", "IO"…); without it, or when
// it returns an empty name, the thread's id.
using LogThreadNamer = std::string (*)();
void SetLogThreadNamer(LogThreadNamer namer);

// One line of the log, as written: local time, level, thread, source
// file:line and the message (line breaks shown as "⏎").
std::string FormatLogLine(std::chrono::system_clock::time_point time,
                          LogLevel level,
                          std::string_view thread,
                          const std::source_location& where,
                          std::string_view message);

// A URL as it may appear in the log: without query, fragment and user name
// or password, which can carry tokens ("…/login?pw=…").
std::string UrlForLog(std::string_view url);

}  // namespace brouk

#endif  // BROUK_CORE_LOG_H_
