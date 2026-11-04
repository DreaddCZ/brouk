#include "core/site_permission_names.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace brouk {

namespace {

constexpr std::array<std::pair<PermissionKind, std::string_view>, 13> kNames = {
    {
        {PermissionKind::kCamera, "media_stream_camera"},
        {PermissionKind::kMicrophone, "media_stream_mic"},
        {PermissionKind::kLocation, "geolocation"},
        {PermissionKind::kNotifications, "notifications"},
        {PermissionKind::kClipboard, "clipboard"},
        {PermissionKind::kMidi, "midi_sysex"},
        {PermissionKind::kWindowManagement, "window_placement"},
        {PermissionKind::kLocalFonts, "local_fonts"},
        {PermissionKind::kIdleDetection, "idle_detection"},
        {PermissionKind::kStorageAccess, "storage_access"},
        {PermissionKind::kCameraPanTiltZoom, "camera_pan_tilt_zoom"},
        {PermissionKind::kPopups, "popups"},
        {PermissionKind::kAutomaticDownloads, "automatic_downloads"},
    }};

bool IsHostChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' ||
         c == '.' || c == '_' || c == '[' || c == ']' || c == ':';
}

}  // namespace

std::string_view ContentSettingName(PermissionKind kind) {
  for (const auto& [known, name] : kNames) {
    if (known == kind) {
      return name;
    }
  }
  return {};
}

std::optional<PermissionKind> PermissionKindFromContentSettingName(
    std::string_view name) {
  for (const auto& [kind, known] : kNames) {
    if (known == name) {
      return kind;
    }
  }
  return std::nullopt;
}

std::optional<std::string> OriginFromSitePattern(std::string_view pattern) {
  const std::size_t comma = pattern.find(',');
  const std::string_view primary = pattern.substr(0, comma);
  if (comma != std::string_view::npos) {
    const std::string_view secondary = pattern.substr(comma + 1);
    if (secondary != "*" && secondary != primary) {
      return std::nullopt;
    }
  }
  std::string_view scheme;
  std::string_view default_port;
  if (primary.starts_with("https://")) {
    scheme = "https";
    default_port = "443";
  } else if (primary.starts_with("http://")) {
    scheme = "http";
    default_port = "80";
  } else {
    return std::nullopt;
  }
  std::string_view host_port = primary.substr(scheme.size() + 3);
  if (host_port.empty() || host_port.starts_with('.') ||
      !std::ranges::all_of(host_port, IsHostChar)) {
    return std::nullopt;  // Also "[*.]example.com" and paths.
  }
  // The port follows the last colon after any IPv6 brackets.
  const std::size_t bracket = host_port.rfind(']');
  const std::size_t colon = host_port.rfind(':');
  std::string_view host = host_port;
  if (colon != std::string_view::npos &&
      (bracket == std::string_view::npos || colon > bracket)) {
    host = host_port.substr(0, colon);
    const std::string_view port = host_port.substr(colon + 1);
    if (port.empty() || !std::ranges::all_of(port, [](char c) {
          return std::isdigit(static_cast<unsigned char>(c)) != 0;
        })) {
      return std::nullopt;
    }
    if (port != default_port) {
      host = host_port;
    }
  }
  if (host.empty()) {
    return std::nullopt;
  }
  return std::string(scheme) + "://" + std::string(host);
}

}  // namespace brouk
