#ifndef BROUK_CEF_PERMISSION_MAPPING_H_
#define BROUK_CEF_PERMISSION_MAPPING_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/permissions.h"
#include "include/cef_permission_handler.h"
#include "include/internal/cef_types_content_settings.h"

namespace brouk {

// Kinds in a CefPermissionHandler::OnShowPermissionPrompt mask
// (cef_permission_request_types_t).
std::vector<PermissionKind> KindsFromPromptMask(std::uint32_t mask);

// Kinds in a media access mask (cef_media_access_permission_types_t).
std::vector<PermissionKind> KindsFromMediaMask(std::uint32_t mask);

// Chromium's content setting that stores a kind for a site, if any.
std::optional<cef_content_setting_types_t> ContentSettingFor(
    PermissionKind kind);

// The kinds shown in site settings (address bar site info), in order.
const std::vector<PermissionKind>& SiteSettingKinds();

// "https://example.com:8443/" -> "example.com:8443" for prompts: the https
// scheme is implied, http stays visible as a warning sign.
std::string OriginForPrompt(const std::string& origin_url);

// The origin ("https://example.com:8443") of a URL, or empty if it has none.
std::string OriginOf(const std::string& url);

}  // namespace brouk

#endif  // BROUK_CEF_PERMISSION_MAPPING_H_
