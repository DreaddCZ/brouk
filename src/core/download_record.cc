#include "core/download_record.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace brouk {

std::string DownloadRecord::FileName() const {
  std::string_view source = path.empty() ? std::string_view(url) : path;
  if (path.empty()) {
    source = source.substr(0, source.find_first_of("?#"));
  }
  std::size_t slash = source.find_last_of('/');
  std::string_view name =
      slash == std::string_view::npos ? source : source.substr(slash + 1);
  return name.empty() ? std::string("download") : std::string(name);
}

std::string_view DownloadStateName(DownloadRecord::State state) {
  switch (state) {
    case DownloadRecord::State::kInProgress:
      return "in_progress";
    case DownloadRecord::State::kPaused:
      return "paused";
    case DownloadRecord::State::kComplete:
      return "complete";
    case DownloadRecord::State::kFailed:
      return "failed";
    case DownloadRecord::State::kCancelled:
      return "cancelled";
  }
  return "failed";
}

DownloadRecord::State DownloadStateFromName(std::string_view name) {
  for (auto state :
       {DownloadRecord::State::kInProgress, DownloadRecord::State::kPaused,
        DownloadRecord::State::kComplete, DownloadRecord::State::kFailed,
        DownloadRecord::State::kCancelled}) {
    if (DownloadStateName(state) == name) {
      return state;
    }
  }
  return DownloadRecord::State::kFailed;
}

std::string FormatByteSize(std::int64_t bytes) {
  std::array<char, 32> text{};
  if (bytes < 1000) {
    std::snprintf(text.data(), text.size(), "%lld bytes",
                  static_cast<long long>(bytes));
  } else if (bytes < std::int64_t{1000} * 1000) {
    std::snprintf(text.data(), text.size(), "%.1f KB",
                  static_cast<double>(bytes) / 1e3);
  } else if (bytes < std::int64_t{1000} * 1000 * 1000) {
    std::snprintf(text.data(), text.size(), "%.1f MB",
                  static_cast<double>(bytes) / 1e6);
  } else {
    std::snprintf(text.data(), text.size(), "%.2f GB",
                  static_cast<double>(bytes) / 1e9);
  }
  return text.data();
}

std::string FormatTimeLeft(std::int64_t seconds) {
  if (seconds < 60) {
    return std::to_string(std::max<std::int64_t>(seconds, 1)) + " s left";
  }
  if (seconds < 3600) {
    return std::to_string((seconds + 30) / 60) + " min left";
  }
  return std::to_string(seconds / 3600) + " h " +
         std::to_string((seconds % 3600) / 60) + " min left";
}

std::string DownloadStatusText(const DownloadRecord& record) {
  switch (record.state) {
    case DownloadRecord::State::kComplete:
      return "Done, " + FormatByteSize(record.received);
    case DownloadRecord::State::kCancelled:
      return "Cancelled";
    case DownloadRecord::State::kFailed:
      return record.error.empty() ? std::string("Failed")
                                  : "Failed — " + record.error;
    case DownloadRecord::State::kPaused:
    case DownloadRecord::State::kInProgress:
      break;
  }
  // "12.3 / 40 MB": the received amount in the total's unit when known.
  std::string amount = FormatByteSize(record.received);
  if (record.total > 0) {
    std::string total = FormatByteSize(record.total);
    std::size_t space = total.find(' ');
    std::string unit = space == std::string::npos ? "" : total.substr(space);
    std::size_t mine = amount.find(' ');
    if (mine != std::string::npos && amount.substr(mine) == unit) {
      amount = amount.substr(0, mine);
    }
    amount += " / " + total;
  }
  if (record.state == DownloadRecord::State::kPaused) {
    return "Paused, " + amount;
  }
  if (record.total > 0 && record.speed > 0 && record.received < record.total) {
    return amount + ", " +
           FormatTimeLeft((record.total - record.received) / record.speed);
  }
  return amount;
}

std::string DownloadInterruptText(int reason) {
  // Values of cef_download_interrupt_reason_t (Chromium's).
  if (reason >= 1 && reason <= 19) {
    switch (reason) {
      case 3:  // FILE_NO_SPACE
        return "disk full";
      case 5:  // FILE_NAME_TOO_LONG
        return "file name too long";
      case 6:  // FILE_TOO_LARGE
        return "file too large";
      case 2:  // FILE_ACCESS_DENIED
        return "no permission to save there";
      case 11:  // FILE_BLOCKED
        return "blocked by the system";
      default:
        return "file error";
    }
  }
  if (reason >= 20 && reason <= 29) {
    return reason == 21   ? "network timeout"
           : reason == 22 ? "network disconnected"
                          : "network error";
  }
  if (reason >= 30 && reason <= 39) {
    switch (reason) {
      case 33:  // SERVER_BAD_CONTENT (404 and similar)
        return "file not found on the server";
      case 34:  // SERVER_UNAUTHORIZED
        return "needs a sign-in";
      case 36:  // SERVER_FORBIDDEN
        return "access denied by the server";
      default:
        return "server error";
    }
  }
  if (reason == 41) {
    return "shut down while downloading";
  }
  if (reason == 50) {  // CRASH
    return "Brouk quit unexpectedly";
  }
  return "failed";
}

std::vector<DownloadRecord> RecentDownloads(
    const std::vector<DownloadRecord>& records,
    std::int64_t now,
    std::int64_t seconds) {
  std::vector<DownloadRecord> recent;
  for (const DownloadRecord& record : records) {
    if (record.Running() || record.start_time >= now - seconds) {
      recent.push_back(record);
    }
  }
  std::ranges::sort(recent, [](const auto& a, const auto& b) {
    return a.start_time > b.start_time ||
           (a.start_time == b.start_time && a.id > b.id);
  });
  return recent;
}

}  // namespace brouk
