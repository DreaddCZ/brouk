#ifndef BROUK_CORE_SETTINGS_EXPORT_H_
#define BROUK_CORE_SETTINGS_EXPORT_H_

#include <string>
#include <string_view>
#include <vector>

#include "core/result.h"
#include "core/settings.h"

namespace brouk {

// A settings export file (004), for moving the global settings to another
// Mac (profiles have their own export, see profile_export.h):
//
//   format: brouk-settings
//   version: 1
//   exported_at: "..."
//   settings: { ... the global settings ... }
inline constexpr std::string_view kSettingsExportExtension = "brouk-settings";
inline constexpr int kSettingsExportVersion = 1;

std::string ExportSettings(const Settings& global,
                           std::string_view exported_at);

struct ImportedSettings {
  Settings settings;
  std::vector<std::string> problems;  // What was not imported.
};

// Fails for other files and newer versions; invalid values keep their
// defaults and are listed in `problems`.
Result<ImportedSettings> ParseSettingsExport(std::string_view text);

}  // namespace brouk

#endif  // BROUK_CORE_SETTINGS_EXPORT_H_
