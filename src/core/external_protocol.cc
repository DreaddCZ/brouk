#include "core/external_protocol.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace brouk {

namespace {

// Schemes the browser handles itself.
constexpr std::array<std::string_view, 14> kBrowserSchemes = {
    "http",       "https",      "ws",          "wss",          "file",
    "data",       "blob",       "about",       "chrome",       "devtools",
    "filesystem", "javascript", "view-source", "chrome-error",
};

// Chrome's ExternalProtocolHandler deny list: schemes that could run code
// or reach local files if handed to the OS. Plus our internal pages
// (brouk://): they run only in their own windows, never in a web tab.
constexpr std::array<std::string_view, 13> kBlockedSchemes = {
    "afp", "disk",  "disks",    "hcp",          "ie.http", "ms-help", "nntp",
    "res", "shell", "vbscript", "vnd.ms.radio", "cid",     "brouk",
};

bool IsSchemeChar(char c, bool first) {
  bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  if (first) {
    return alpha;
  }
  return alpha || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

// Chrome's limit for URLs handed to ShellExecute.
constexpr std::size_t kMaxShellUrlLength = 2048;

// Characters Chrome escapes in URLs for other apps (besides control
// characters, space and non-ASCII).
constexpr std::string_view kShellUnsafe = "\"<>\\^`{|}";

}  // namespace

std::string SchemeOf(std::string_view url) {
  std::size_t colon = url.find(':');
  if (colon == std::string_view::npos || colon == 0) {
    return {};
  }
  std::string scheme;
  for (std::size_t i = 0; i < colon; ++i) {
    char c = url[i];
    if (!IsSchemeChar(c, i == 0)) {
      return {};
    }
    scheme.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a')
                                          : c);
  }
  return scheme;
}

SchemeHandling ClassifyScheme(std::string_view url) {
  std::string scheme = SchemeOf(url);
  if (scheme.empty() ||
      std::ranges::find(kBrowserSchemes, scheme) != kBrowserSchemes.end()) {
    return SchemeHandling::kBrowser;
  }
  if (std::ranges::find(kBlockedSchemes, scheme) != kBlockedSchemes.end()) {
    return SchemeHandling::kBlocked;
  }
  return SchemeHandling::kExternal;
}

std::string ExternalProtocolAllowKey(std::string_view origin,
                                     std::string_view scheme) {
  return std::string(origin) + " " + std::string(scheme);
}

std::optional<std::string> ExternalUrlForShell(std::string_view url) {
  if (SchemeOf(url).size() < 2) {
    return std::nullopt;
  }
  constexpr std::string_view kHex = "0123456789ABCDEF";
  std::string escaped;
  escaped.reserve(url.size());
  for (char c : url) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte <= 0x20 || byte >= 0x7F ||
        kShellUnsafe.find(c) != std::string_view::npos) {
      escaped.push_back('%');
      escaped.push_back(kHex[byte >> 4]);
      escaped.push_back(kHex[byte & 0xF]);
    } else {
      escaped.push_back(c);
    }
  }
  if (escaped.size() > kMaxShellUrlLength) {
    return std::nullopt;
  }
  return escaped;
}

}  // namespace brouk
