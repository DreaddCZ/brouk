#include "cef/permission_mapping.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_parser.h"

namespace brouk {

namespace {

constexpr std::array<std::pair<std::uint32_t, PermissionKind>, 12>
    kPromptKinds = {{
        {CEF_PERMISSION_TYPE_CAMERA_STREAM, PermissionKind::kCamera},
        {CEF_PERMISSION_TYPE_MIC_STREAM, PermissionKind::kMicrophone},
        {CEF_PERMISSION_TYPE_GEOLOCATION, PermissionKind::kLocation},
        {CEF_PERMISSION_TYPE_NOTIFICATIONS, PermissionKind::kNotifications},
        {CEF_PERMISSION_TYPE_CLIPBOARD, PermissionKind::kClipboard},
        {CEF_PERMISSION_TYPE_MIDI_SYSEX, PermissionKind::kMidi},
        {CEF_PERMISSION_TYPE_WINDOW_MANAGEMENT,
         PermissionKind::kWindowManagement},
        {CEF_PERMISSION_TYPE_LOCAL_FONTS, PermissionKind::kLocalFonts},
        {CEF_PERMISSION_TYPE_IDLE_DETECTION, PermissionKind::kIdleDetection},
        {CEF_PERMISSION_TYPE_STORAGE_ACCESS, PermissionKind::kStorageAccess},
        {CEF_PERMISSION_TYPE_TOP_LEVEL_STORAGE_ACCESS,
         PermissionKind::kStorageAccess},
        {CEF_PERMISSION_TYPE_CAMERA_PAN_TILT_ZOOM,
         PermissionKind::kCameraPanTiltZoom},
    }};

}  // namespace

std::vector<PermissionKind> KindsFromPromptMask(std::uint32_t mask) {
  std::vector<PermissionKind> kinds;
  std::uint32_t known = 0;
  for (const auto& [bit, kind] : kPromptKinds) {
    known |= bit;
    if ((mask & bit) != 0 && std::ranges::find(kinds, kind) == kinds.end()) {
      kinds.push_back(kind);
    }
  }
  if ((mask & ~known) != 0) {
    kinds.push_back(PermissionKind::kOther);
  }
  return kinds;
}

std::vector<PermissionKind> KindsFromMediaMask(std::uint32_t mask) {
  std::vector<PermissionKind> kinds;
  if (mask & CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE) {
    kinds.push_back(PermissionKind::kCamera);
  }
  if (mask & CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE) {
    kinds.push_back(PermissionKind::kMicrophone);
  }
  if (mask & (CEF_MEDIA_PERMISSION_DESKTOP_VIDEO_CAPTURE |
              CEF_MEDIA_PERMISSION_DESKTOP_AUDIO_CAPTURE)) {
    kinds.push_back(PermissionKind::kScreenShare);
  }
  return kinds;
}

std::optional<cef_content_setting_types_t> ContentSettingFor(
    PermissionKind kind) {
  switch (kind) {
    case PermissionKind::kCamera:
      return CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_CAMERA;
    case PermissionKind::kMicrophone:
      return CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_MIC;
    case PermissionKind::kLocation:
      return CEF_CONTENT_SETTING_TYPE_GEOLOCATION;
    case PermissionKind::kNotifications:
      return CEF_CONTENT_SETTING_TYPE_NOTIFICATIONS;
    case PermissionKind::kClipboard:
      return CEF_CONTENT_SETTING_TYPE_CLIPBOARD_READ_WRITE;
    case PermissionKind::kMidi:
      return CEF_CONTENT_SETTING_TYPE_MIDI_SYSEX;
    case PermissionKind::kWindowManagement:
      return CEF_CONTENT_SETTING_TYPE_WINDOW_MANAGEMENT;
    case PermissionKind::kLocalFonts:
      return CEF_CONTENT_SETTING_TYPE_LOCAL_FONTS;
    case PermissionKind::kIdleDetection:
      return CEF_CONTENT_SETTING_TYPE_IDLE_DETECTION;
    case PermissionKind::kStorageAccess:
      return CEF_CONTENT_SETTING_TYPE_STORAGE_ACCESS;
    case PermissionKind::kCameraPanTiltZoom:
      return CEF_CONTENT_SETTING_TYPE_CAMERA_PAN_TILT_ZOOM;
    case PermissionKind::kPopups:
      return CEF_CONTENT_SETTING_TYPE_POPUPS;
    case PermissionKind::kScreenShare:
    case PermissionKind::kOther:
      return std::nullopt;
  }
  return std::nullopt;
}

const std::vector<PermissionKind>& SiteSettingKinds() {
  static const std::vector<PermissionKind> kKinds = {
      PermissionKind::kCamera,           PermissionKind::kMicrophone,
      PermissionKind::kLocation,         PermissionKind::kNotifications,
      PermissionKind::kClipboard,        PermissionKind::kMidi,
      PermissionKind::kWindowManagement, PermissionKind::kLocalFonts,
      PermissionKind::kIdleDetection,    PermissionKind::kPopups,
  };
  return kKinds;
}

std::string OriginForPrompt(const std::string& origin_url) {
  if (origin_url.empty()) {
    return {};  // CEF's URL functions require a non-empty URL.
  }
  std::string display = CefFormatUrlForSecurityDisplay(origin_url).ToString();
  constexpr std::string_view kHttps = "https://";
  if (display.starts_with(kHttps)) {
    display.erase(0, kHttps.size());
  }
  while (!display.empty() && display.back() == '/') {
    display.pop_back();
  }
  return display;
}

std::string OriginOf(const std::string& url) {
  if (url.empty()) {
    return {};  // CEF's URL functions require a non-empty URL.
  }
  CefURLParts parts;
  if (!CefParseURL(url, parts)) {
    return {};
  }
  std::string origin = CefString(&parts.origin).ToString();
  while (!origin.empty() && origin.back() == '/') {
    origin.pop_back();
  }
  return origin;
}

}  // namespace brouk
