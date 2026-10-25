#ifndef BROUK_CORE_PERMISSIONS_H_
#define BROUK_CORE_PERMISSIONS_H_

#include <string>
#include <string_view>
#include <vector>

namespace brouk {

// Site permissions the browser asks the user about (012, item 3).
enum class PermissionKind {
  kCamera,
  kMicrophone,
  kScreenShare,
  kLocation,
  kNotifications,
  kClipboard,
  kMidi,
  kWindowManagement,
  kLocalFonts,
  kIdleDetection,
  kStorageAccess,
  kCameraPanTiltZoom,
  kPopups,  // Pop-ups and redirects (blocked by default, as in Chrome).
  // Several downloads without a click in between (014, as in Chrome).
  kAutomaticDownloads,
  kOther,
};

// The user's answer to a permission prompt.
enum class PermissionChoice {
  kAllow,      // "Allow while visiting the site" / "Allow": remembered.
  kAllowOnce,  // "Allow this time": until the page is left or the tab closes.
  kBlock,      // "Never allow" / "Block": remembered.
  kDismiss,    // Prompt closed without a choice: ask again next time.
};

// One line of the prompt, as in Chrome: "Use your camera".
std::string PermissionDescription(PermissionKind kind);

// Short name for site settings: "Camera".
std::string PermissionName(PermissionKind kind);

// Whether the prompt offers "Allow this time". Chrome offers one-time grants
// for camera, microphone and location only.
bool OffersAllowOnce(const std::vector<PermissionKind>& kinds);

// Whether a choice for these kinds is remembered for the site. Screen
// sharing is never remembered: the site asks every time, as in Chrome.
bool IsRemembered(const std::vector<PermissionKind>& kinds);

// Button titles for the prompt, in display order, with their choices.
struct PermissionButton {
  std::string title;
  PermissionChoice choice;
};
std::vector<PermissionButton> PermissionButtons(
    const std::vector<PermissionKind>& kinds);

// Prompt title: "example.com wants to".
std::string PermissionPromptTitle(std::string_view origin_display);

}  // namespace brouk

#endif  // BROUK_CORE_PERMISSIONS_H_
