#include "core/download_paths.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace brouk {

namespace {

constexpr std::size_t kMaxFileNameBytes = 255;
constexpr int kMaxNumberedAttempts = 10000;
constexpr std::string_view kFallbackName = "download";

constexpr std::array<std::string_view, 5> kCompoundExtensions = {
    ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tar.Z"};

bool EndsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() &&
         s.substr(s.size() - suffix.size()) == suffix;
}

// Position where the extension starts (".pdf", ".tar.gz"), or npos.
std::size_t ExtensionStart(std::string_view name) {
  for (std::string_view compound : kCompoundExtensions) {
    if (name.size() > compound.size() && EndsWith(name, compound)) {
      return name.size() - compound.size();
    }
  }
  std::size_t dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0) {
    return std::string_view::npos;
  }
  return dot;
}

// Cuts a UTF-8 string to at most `max_bytes` without splitting a character.
std::string TruncateUtf8(std::string s, std::size_t max_bytes) {
  if (s.size() <= max_bytes) {
    return s;
  }
  std::size_t end = max_bytes;
  while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) {
    --end;  // Step back over continuation bytes.
  }
  s.resize(end);
  return s;
}

}  // namespace

std::string SanitizeDownloadFileName(std::string_view suggested) {
  // Only the last path component counts, for both separators.
  std::size_t slash = suggested.find_last_of("/\\");
  if (slash != std::string_view::npos) {
    suggested.remove_prefix(slash + 1);
  }
  std::string name;
  name.reserve(suggested.size());
  for (char c : suggested) {
    auto byte = static_cast<unsigned char>(c);
    // Control characters and ':' (the Finder shows it as '/') become '_'.
    name.push_back(byte < 0x20 || byte == 0x7F || c == ':' ? '_' : c);
  }
  // No leading dots (hidden files, "..") or spaces, no trailing dots/spaces.
  std::size_t begin = name.find_first_not_of(". ");
  if (begin == std::string::npos) {
    return std::string(kFallbackName);
  }
  std::size_t last = name.find_last_not_of(". ");
  name = name.substr(begin, last - begin + 1);
  if (name.size() > kMaxFileNameBytes) {
    // Keep the extension so the file still opens with the right app.
    std::size_t ext = ExtensionStart(name);
    std::string extension =
        ext == std::string::npos ? std::string() : name.substr(ext);
    if (extension.size() > 32) {
      extension.clear();
    }
    std::string stem =
        name.substr(0, ext == std::string::npos ? name.size() : ext);
    name = TruncateUtf8(stem, kMaxFileNameBytes - extension.size()) + extension;
  }
  return name;
}

std::string NumberedFileName(std::string_view file_name, int number) {
  std::size_t ext = ExtensionStart(file_name);
  std::string_view stem =
      ext == std::string_view::npos ? file_name : file_name.substr(0, ext);
  std::string_view extension = ext == std::string_view::npos
                                   ? std::string_view()
                                   : file_name.substr(ext);
  return std::string(stem) + " (" + std::to_string(number) + ")" +
         std::string(extension);
}

std::string DownloadPathReserver::Reserve(std::string_view directory,
                                          std::string_view suggested) {
  std::string name = SanitizeDownloadFileName(suggested);
  std::string dir(directory);
  if (!dir.empty() && dir.back() != '/') {
    dir.push_back('/');
  }
  auto taken = [this](const std::string& path) {
    return reserved_.contains(path) || exists_(path);
  };
  std::string path = dir + name;
  for (int number = 1; taken(path) && number <= kMaxNumberedAttempts;
       ++number) {
    path = dir + NumberedFileName(name, number);
  }
  reserved_.insert(path);
  return path;
}

void DownloadPathReserver::Release(const std::string& path) {
  reserved_.erase(path);
}

}  // namespace brouk
