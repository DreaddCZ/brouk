#include "core/internal_pages.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace brouk {

namespace {

// Pages that exist. New pages are added here.
constexpr std::array<std::string_view, 4> kPages = {"history", "profiles",
                                                    "settings", "tools"};

constexpr std::array<std::string_view, 4> kExtensions = {".html", ".js", ".css",
                                                         ".svg"};

struct Parsed {
  std::string_view page;
  std::string_view path;  // After the page, without the leading '/'.
};

std::optional<Parsed> Parse(std::string_view url) {
  constexpr std::string_view kPrefix = "brouk://";
  if (!url.starts_with(kPrefix)) {
    return std::nullopt;
  }
  url.remove_prefix(kPrefix.size());
  // Queries and fragments do not change which file is served.
  url = url.substr(0, url.find_first_of("?#"));
  std::size_t slash = url.find('/');
  std::string_view page = url.substr(0, slash);
  if (std::ranges::find(kPages, page) == kPages.end()) {
    return std::nullopt;
  }
  std::string_view path = slash == std::string_view::npos
                              ? std::string_view()
                              : url.substr(slash + 1);
  return Parsed{page, path};
}

bool IsPlainFileName(std::string_view name) {
  return !name.empty() && name.front() != '.' &&
         std::ranges::all_of(name, [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
         });
}

bool HasKnownExtension(std::string_view name) {
  return std::ranges::any_of(kExtensions, [name](std::string_view ext) {
    return name.size() > ext.size() && name.ends_with(ext);
  });
}

}  // namespace

bool IsInternalPageUrl(std::string_view url) {
  return Parse(url).has_value();
}

std::optional<std::string> InternalPageOf(std::string_view url) {
  std::optional<Parsed> parsed = Parse(url);
  if (!parsed) {
    return std::nullopt;
  }
  return std::string(parsed->page);
}

std::optional<std::string> InternalResourcePath(std::string_view url) {
  std::optional<Parsed> parsed = Parse(url);
  if (!parsed) {
    return std::nullopt;
  }
  std::string_view path = parsed->path;
  if (path.empty()) {
    return std::string(parsed->page) + "/index.html";
  }
  std::string_view folder = parsed->page;
  constexpr std::string_view kShared = "shared/";
  if (path.starts_with(kShared)) {
    folder = "shared";
    path.remove_prefix(kShared.size());
  }
  // One plain file name: no further folders, so nothing outside web/.
  if (!IsPlainFileName(path) || !HasKnownExtension(path)) {
    return std::nullopt;
  }
  return std::string(folder) + "/" + std::string(path);
}

std::string_view InternalResourceMimeType(std::string_view path) {
  if (path.ends_with(".html")) {
    return "text/html";
  }
  if (path.ends_with(".js")) {
    return "text/javascript";
  }
  if (path.ends_with(".css")) {
    return "text/css";
  }
  if (path.ends_with(".svg")) {
    return "image/svg+xml";
  }
  return "application/octet-stream";
}

}  // namespace brouk
