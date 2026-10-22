#ifndef BROUK_CORE_SETTINGS_FILE_H_
#define BROUK_CORE_SETTINGS_FILE_H_

#include <filesystem>
#include <optional>
#include <vector>

#include "core/result.h"
#include "core/settings.h"

namespace brouk {

struct LoadedSettings {
  Settings settings;
  // What was wrong in the file; shown to the user (004: "invalid values are
  // reported, not silently ignored").
  std::vector<SettingProblem> problems;
};

// Reads a settings file. A missing file gives the defaults without problems;
// a file that cannot be read or parsed gives the defaults and a problem.
LoadedSettings LoadSettingsFile(const std::filesystem::path& path,
                                SettingScope scope);

// Writes all settings (machine-written: comments and key order the user
// added are not kept, 004). The file is replaced atomically and readable by
// the user only.
std::optional<Error> SaveSettingsFile(const std::filesystem::path& path,
                                      const Settings& settings);

}  // namespace brouk

#endif  // BROUK_CORE_SETTINGS_FILE_H_
