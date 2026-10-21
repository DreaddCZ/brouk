#include "core/permissions.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace brouk {

std::string PermissionDescription(PermissionKind kind) {
  switch (kind) {
    case PermissionKind::kCamera:
      return "Use your camera";
    case PermissionKind::kMicrophone:
      return "Use your microphone";
    case PermissionKind::kScreenShare:
      return "Share your screen";
    case PermissionKind::kLocation:
      return "Know your location";
    case PermissionKind::kNotifications:
      return "Show notifications";
    case PermissionKind::kClipboard:
      return "See text and images copied to the clipboard";
    case PermissionKind::kMidi:
      return "Control and reprogram your MIDI devices";
    case PermissionKind::kWindowManagement:
      return "Manage windows on all your displays";
    case PermissionKind::kLocalFonts:
      return "Use your computer's fonts";
    case PermissionKind::kIdleDetection:
      return "Know when you're actively using this device";
    case PermissionKind::kStorageAccess:
      return "Use cookies and site data from other sites";
    case PermissionKind::kCameraPanTiltZoom:
      return "Use and move your camera";
    case PermissionKind::kPopups:
      return "Open pop-ups and redirects";
    case PermissionKind::kOther:
      return "Use a browser feature";
  }
  return "Use a browser feature";
}

std::string PermissionName(PermissionKind kind) {
  switch (kind) {
    case PermissionKind::kCamera:
      return "Camera";
    case PermissionKind::kMicrophone:
      return "Microphone";
    case PermissionKind::kScreenShare:
      return "Screen sharing";
    case PermissionKind::kLocation:
      return "Location";
    case PermissionKind::kNotifications:
      return "Notifications";
    case PermissionKind::kClipboard:
      return "Clipboard";
    case PermissionKind::kMidi:
      return "MIDI devices";
    case PermissionKind::kWindowManagement:
      return "Window management";
    case PermissionKind::kLocalFonts:
      return "Fonts";
    case PermissionKind::kIdleDetection:
      return "Your device use";
    case PermissionKind::kStorageAccess:
      return "Third-party cookies";
    case PermissionKind::kCameraPanTiltZoom:
      return "Camera movement";
    case PermissionKind::kPopups:
      return "Pop-ups and redirects";
    case PermissionKind::kOther:
      return "Other";
  }
  return "Other";
}

bool OffersAllowOnce(const std::vector<PermissionKind>& kinds) {
  if (kinds.empty()) {
    return false;
  }
  return std::ranges::all_of(kinds, [](PermissionKind kind) {
    return kind == PermissionKind::kCamera ||
           kind == PermissionKind::kMicrophone ||
           kind == PermissionKind::kLocation ||
           kind == PermissionKind::kCameraPanTiltZoom;
  });
}

bool IsRemembered(const std::vector<PermissionKind>& kinds) {
  return std::ranges::none_of(kinds, [](PermissionKind kind) {
    return kind == PermissionKind::kScreenShare;
  });
}

std::vector<PermissionButton> PermissionButtons(
    const std::vector<PermissionKind>& kinds) {
  if (!IsRemembered(kinds)) {
    return {{"Allow", PermissionChoice::kAllowOnce},
            {"Don't allow", PermissionChoice::kDismiss}};
  }
  if (OffersAllowOnce(kinds)) {
    return {{"Allow while visiting the site", PermissionChoice::kAllow},
            {"Allow this time", PermissionChoice::kAllowOnce},
            {"Never allow", PermissionChoice::kBlock}};
  }
  return {{"Allow", PermissionChoice::kAllow},
          {"Block", PermissionChoice::kBlock}};
}

std::string PermissionPromptTitle(std::string_view origin_display) {
  return std::string(origin_display) + " wants to";
}

}  // namespace brouk
