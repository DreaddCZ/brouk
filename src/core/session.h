#ifndef BROUK_CORE_SESSION_H_
#define BROUK_CORE_SESSION_H_

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/profile_id.h"
#include "core/result.h"
#include "core/yaml_value.h"

namespace brouk {

// The windows and tabs open in a run, saved while Brouk runs so the next
// start can bring them back (015).
struct SessionTab {
  std::string url;
  std::string title;
  ProfileId profile;
  bool pinned = false;
};

struct SessionWindow {
  std::vector<SessionTab> tabs = {};  // Left to right.
  std::size_t active = 0;             // Index into `tabs`.
  std::vector<ProfileId> collapsed = {};
  // Screen frame in whole points; width 0 if unknown.
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct Session {
  std::vector<SessionWindow> windows = {};  // Back to front.
  // False while Brouk runs; true after a normal quit. A start that finds
  // false knows the last run crashed (015: "Restore pages?").
  bool clean_exit = true;
};

YamlNode SessionToYaml(const Session& session);
// The file is machine-written but read like any file: entries that are not
// valid (no URL, unknown keys, absurd sizes) are skipped, never trusted.
Session SessionFromYaml(const YamlNode& root);

// A missing file is an empty session after a clean exit.
Result<Session> LoadSession(const std::filesystem::path& path);
// Written to a temporary file and renamed, so a crash never leaves half a
// file.
std::optional<Error> SaveSession(const std::filesystem::path& path,
                                 const Session& session);

// What the start does (015), from the setting startup.restore and the last
// session.
enum class StartupAction {
  kNewWindow,              // A new window with a new tab.
  kRestore,                // The last session's windows.
  kNewWindowOfferRestore,  // A new window with a "Restore pages?" bar.
};
StartupAction DecideStartup(std::string_view restore_setting,
                            const Session& last);

}  // namespace brouk

#endif  // BROUK_CORE_SESSION_H_
