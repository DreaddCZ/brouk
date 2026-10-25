#ifndef BROUK_CORE_DOWNLOAD_RECORD_H_
#define BROUK_CORE_DOWNLOAD_RECORD_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace brouk {

// One download in a profile's downloads list (014), stored in its history
// database (013).
struct DownloadRecord {
  enum class State { kInProgress, kPaused, kComplete, kFailed, kCancelled };

  std::int64_t id = 0;  // Our id, stable across runs (not CEF's).
  std::string url;      // The final URL.
  std::string original_url;
  std::string referrer_site;  // The page's host, for display ("example.com").
  std::string path;           // Full path of the file ("" until chosen).
  std::string mime_type;
  State state = State::kInProgress;
  std::string error;  // kFailed: why, for the user.
  std::int64_t received = 0;
  std::int64_t total = 0;       // 0 when unknown.
  std::int64_t speed = 0;       // Bytes per second while in progress.
  std::int64_t start_time = 0;  // Seconds since 1970.
  std::int64_t end_time = 0;
  bool opened = false;  // The user saw it finished (the button's dot).

  bool Running() const {
    return state == State::kInProgress || state == State::kPaused;
  }
  // The file name from `path` (or from the URL before there is a path).
  std::string FileName() const;
};

std::string_view DownloadStateName(DownloadRecord::State state);
DownloadRecord::State DownloadStateFromName(std::string_view name);

// "12.3 / 40 MB, 2 min left", "Paused, 12.3 / 40 MB", "Done, 40 MB",
// "Failed — network error", "Cancelled" (014).
std::string DownloadStatusText(const DownloadRecord& record);
// "1.2 KB", "12.3 MB", "1.1 GB" (decimal units, as macOS shows sizes).
std::string FormatByteSize(std::int64_t bytes);
// "45 s left", "2 min left", "1 h 5 min left".
std::string FormatTimeLeft(std::int64_t seconds);

// Chromium's interrupt reasons (cef_download_interrupt_reason_t numbers)
// as short texts: "network error", "disk full", "file not found on the
// server", …
std::string DownloadInterruptText(int reason);

// The downloads the popup shows: those started within `seconds` before
// `now`, and running ones; newest first.
std::vector<DownloadRecord> RecentDownloads(
    const std::vector<DownloadRecord>& records,
    std::int64_t now,
    std::int64_t seconds);

}  // namespace brouk

#endif  // BROUK_CORE_DOWNLOAD_RECORD_H_
