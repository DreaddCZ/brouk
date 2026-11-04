#include "core/log.h"

#include <atomic>
#include <cstdio>
#include <ctime>
#include <functional>
#include <iomanip>
#include <mutex>
#include <thread>
#include <utility>

namespace brouk {

namespace {

std::atomic<bool> debug_logging{false};
std::atomic<bool> log_to_stderr{true};
std::atomic<LogThreadNamer> thread_namer{nullptr};

// The open log file. Guarded by its mutex: messages come from every thread.
struct LogState {
  std::mutex mutex;
  std::optional<RotatingLogFile> file;  // Guarded by `mutex`.
};

LogState& State() {
  static LogState state;  // Never destroyed before the last message.
  return state;
}

std::string_view LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "DEBUG";
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarning:
      return "WARNING";
    case LogLevel::kError:
      return "ERROR";
  }
  return "?";
}

std::string ThreadName() {
  if (const LogThreadNamer namer = thread_namer.load(); namer) {
    if (std::string name = namer(); !name.empty()) {
      return name;
    }
  }
  std::ostringstream id;
  id << std::this_thread::get_id();
  return id.str();
}

std::string_view FileName(std::string_view path) {
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

void Write(LogLevel level,
           const std::source_location& where,
           std::string_view message) {
  const std::string line = FormatLogLine(std::chrono::system_clock::now(),
                                         level, ThreadName(), where, message);
  if (log_to_stderr.load()) {
    std::fprintf(stderr, "%s\n", line.c_str());
  }
  LogState& state = State();
  const std::scoped_lock lock(state.mutex);
  if (state.file) {
    state.file->Write(line);
  }
}

}  // namespace

LogMessage::LogMessage(LogLevel level, std::source_location where)
    : level_(level),
      where_(where),
      enabled_(level >= LogLevel::kWarning || debug_logging.load()) {}

LogMessage::~LogMessage() {
  if (enabled_) {
    Write(level_, where_, stream_.str());
  }
}

LogMessage LogDebug(std::source_location where) {
  return {LogLevel::kDebug, where};
}

LogMessage LogInfo(std::source_location where) {
  return {LogLevel::kInfo, where};
}

LogMessage LogWarning(std::source_location where) {
  return {LogLevel::kWarning, where};
}

LogMessage LogError(std::source_location where) {
  return {LogLevel::kError, where};
}

std::optional<Error> StartLogFile(const std::filesystem::path& directory,
                                  RotatingLogFile::Limits limits) {
  Result<RotatingLogFile> file =
      RotatingLogFile::Open(directory, "brouk", limits);
  if (!file) {
    return file.error();
  }
  LogState& state = State();
  const std::scoped_lock lock(state.mutex);
  state.file.emplace(std::move(file).value());
  return std::nullopt;
}

void StopLogFile() {
  LogState& state = State();
  const std::scoped_lock lock(state.mutex);
  state.file.reset();
}

void SetDebugLogging(bool on) {
  debug_logging.store(on);
}

bool DebugLoggingOn() {
  return debug_logging.load();
}

void SetLogToStderr(bool on) {
  log_to_stderr.store(on);
}

void SetLogThreadNamer(LogThreadNamer namer) {
  thread_namer.store(namer);
}

std::string FormatLogLine(std::chrono::system_clock::time_point time,
                          LogLevel level,
                          std::string_view thread,
                          const std::source_location& where,
                          std::string_view message) {
  const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
  std::tm local = {};
#if defined(_WIN32)
  localtime_s(&local, &seconds);
#else
  localtime_r(&seconds, &local);
#endif
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                          time.time_since_epoch())
                          .count() %
                      1000;
  std::ostringstream line;
  line << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0')
       << std::setw(3) << (millis < 0 ? 0 : millis) << ' ' << LevelName(level)
       << " [" << thread << "] " << FileName(where.file_name()) << ':'
       << where.line() << ' ';
  // One message, one line: line breaks would make a message look like
  // several.
  for (const char c : message) {
    if (c == '\n') {
      line << "\u23CE";
    } else if (c == '\r') {
      continue;
    } else {
      line << c;
    }
  }
  return line.str();
}

std::string UrlForLog(std::string_view url) {
  std::string_view kept = url.substr(0, url.find_first_of("?#"));
  // user:password@ in the authority.
  const std::size_t scheme = kept.find("://");
  if (scheme != std::string_view::npos) {
    const std::size_t authority = scheme + 3;
    const std::size_t path = kept.find('/', authority);
    const std::string_view host = kept.substr(
        authority, path == std::string_view::npos ? path : path - authority);
    if (const std::size_t at = host.rfind('@'); at != std::string_view::npos) {
      return std::string(kept.substr(0, authority)) +
             std::string(kept.substr(authority + at + 1));
    }
  }
  return std::string(kept);
}

}  // namespace brouk
