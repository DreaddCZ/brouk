#ifndef BROUK_CORE_LAUNCH_ARGS_H_
#define BROUK_CORE_LAUNCH_ARGS_H_

#include <optional>
#include <string>
#include <vector>

namespace brouk {

// What a launch of the app asks for: URLs to open, and the profile to open
// them in (005: `--profile <name>`).
struct LaunchRequest {
  std::vector<std::string> urls;
  std::optional<std::string> profile;  // A profile name or id.
};

// `arguments` are the non-switch arguments of the command line;
// `profile_switch` is the value of --profile if it was given ("" for a
// bare --profile). Chromium's command line parser reads
// `--profile Work` as a bare switch followed by the argument "Work"; then
// the first argument is the profile, not a URL. `--profile=Work` works too.
LaunchRequest ParseLaunchRequest(std::vector<std::string> arguments,
                                 std::optional<std::string> profile_switch);

}  // namespace brouk

#endif  // BROUK_CORE_LAUNCH_ARGS_H_
