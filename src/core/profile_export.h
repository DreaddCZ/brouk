#ifndef BROUK_CORE_PROFILE_EXPORT_H_
#define BROUK_CORE_PROFILE_EXPORT_H_

#include <string>
#include <string_view>
#include <vector>

#include "core/result.h"
#include "core/settings.h"

namespace brouk {

// A profile export file (005), for moving a profile to another device:
//
//   format: brouk-profile
//   version: 1
//   exported_at: "2026-10-22T10:00:00Z"
//   settings:            the profile's settings (name, colour, ...)
//     profile:
//       name: "Work"
//       color: green
//
// Never contains cookies, sign-ins, sessions or cache. Later versions add
// sections (bookmarks after 011, encrypted passwords after 008, history
// after 013); `version` changes only when older Brouk versions could not
// read a file correctly, and such files are refused.
inline constexpr std::string_view kProfileExportExtension = "brouk-profile";
inline constexpr int kProfileExportVersion = 1;

// `exported_at` is an ISO 8601 time, for the user's information.
std::string ExportProfile(const Settings& profile_settings,
                          std::string_view exported_at);

struct ImportedProfile {
  Settings settings;
  // Things in the file that were not imported (invalid values, sections
  // this version cannot read), for the user.
  std::vector<std::string> problems;
};

// Reads an export file. Fails for files that are not profile exports or
// come from a newer version; invalid settings fall back to defaults and are
// listed in `problems`.
Result<ImportedProfile> ParseProfileExport(std::string_view text);

}  // namespace brouk

#endif  // BROUK_CORE_PROFILE_EXPORT_H_
