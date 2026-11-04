#ifndef BROUK_CORE_SITE_PERMISSION_NAMES_H_
#define BROUK_CORE_SITE_PERMISSION_NAMES_H_

#include <optional>
#include <string>
#include <string_view>

#include "core/permissions.h"

namespace brouk {

// Site permissions in settings (012): Chromium keeps a profile's decisions
// in the preference "profile.content_settings.exceptions.<name>", a map
// from a site pattern ("https://example.com:443,*") to the decision. CEF
// can read a site's decision but cannot list the sites, so settings reads
// these preferences.

// Chromium's name of the content setting that stores `kind`
// ("media_stream_camera"); empty for kinds that are not stored. Also the
// kind's id on the settings page.
std::string_view ContentSettingName(PermissionKind kind);

std::optional<PermissionKind> PermissionKindFromContentSettingName(
    std::string_view name);

// The site of a stored decision: "https://example.com:443,*" ->
// "https://example.com". Only patterns for one http(s) origin (as
// Chromium stores a prompt's answer); wildcard patterns give nothing.
std::optional<std::string> OriginFromSitePattern(std::string_view pattern);

}  // namespace brouk

#endif  // BROUK_CORE_SITE_PERMISSION_NAMES_H_
