#include "core/devtools_tools.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace brouk {

namespace {

// What the tools need, nothing more: reading and changing the current
// site's cookies and storage, clearing its data, the tab's requests, and
// reading (not changing) the page's elements for the security check.
constexpr std::array<std::string_view, 23> kAllowed = {
    "CacheStorage.deleteCache",
    "CacheStorage.requestCacheNames",
    "DOM.getAttributes",
    "DOM.getDocument",
    "DOM.querySelectorAll",
    "DOMStorage.clear",
    "DOMStorage.enable",
    "DOMStorage.getDOMStorageItems",
    "DOMStorage.removeDOMStorageItem",
    "DOMStorage.setDOMStorageItem",
    "IndexedDB.deleteDatabase",
    "IndexedDB.enable",
    "IndexedDB.requestDatabaseNames",
    "Network.deleteCookies",
    "Network.getCookies",
    "Network.getRequestPostData",
    "Network.getResponseBody",
    "Network.setCookie",
    "ServiceWorker.enable",
    "ServiceWorker.unregister",
    "Storage.clearDataForOrigin",
    "Storage.getStorageKey",
    "Storage.getUsageAndQuota",
};

}  // namespace

std::span<const DevToolsUtility> DevToolsUtilities() {
  static constexpr std::array<DevToolsUtility, 14> kUtilities = {{
      {"base64", "Base64"},
      {"unix-time", "Unix time"},
      {"url-encoding", "URL encoding"},
      {"url-parser", "URL parser"},
      {"html", "HTML entities"},
      {"hash", "Hashes and HMAC"},
      {"uuid", "UUID"},
      {"json", "JSON and escaping"},
      {"number-bases", "Number bases"},
      {"text-hex", "Text and hex bytes"},
      {"regex", "Regular expressions"},
      {"cron", "Cron expressions"},
      {"color", "Colours"},
      {"environment", "Tab environment"},
  }};
  return kUtilities;
}

bool ToolsPageOpensLink(std::string_view url) {
  constexpr std::array<std::string_view, 2> kPrefixes = {
      "https://cheatsheetseries.owasp.org/",
      "https://owasp.org/",
  };
  return url.size() < 2048 && std::ranges::any_of(
                                  kPrefixes,
                                  [url](std::string_view prefix) {
                                    return url.starts_with(prefix);
                                  });
}

bool IsAllowedToolsProtocolMethod(std::string_view method) {
  return std::ranges::find(kAllowed, method) != kAllowed.end();
}

}  // namespace brouk
