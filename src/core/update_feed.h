#ifndef BROUK_CORE_UPDATE_FEED_H_
#define BROUK_CORE_UPDATE_FEED_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.h"

namespace brouk {

// One release in the Windows update feed (024, W4): the releases
// repository's windows-x64.json, written by scripts/release_windows. macOS
// uses Sparkle's appcasts instead (019).
struct UpdateRelease {
  std::string version;    // "0.3.0"
  std::string channel;    // "stable" or "beta"
  std::string url;        // The installer, https only.
  std::int64_t size = 0;  // The installer's size in bytes.
  std::string signature;  // Ed25519 signature of the installer, raw bytes.
  std::string notes_url;  // The release's page (may be empty).
};

// The feed's releases, in its order. Entries that are not complete and
// valid are left out (a newer feed may have more kinds of entries); the
// feed itself must be a JSON object with a "releases" array.
//   {"releases": [{"version": "0.3.0", "channel": "beta",
//                  "url": "https://…/Brouk-0.3.0-setup.exe", "size": 123,
//                  "ed_signature": "<base64>",
//                  "notes_url": "https://…/tag/v0.3.0"}]}
Result<std::vector<UpdateRelease>> ParseUpdateFeed(std::string_view json);

// "MAJOR.MINOR.PATCH" (digits only), else nullopt.
std::optional<std::array<std::uint32_t, 3>> ParseVersion(std::string_view text);

// The newest release above `current_version` that `channel` gets: "stable"
// gets stable releases, "beta" stable and beta ones. Never an older or the
// same version, so going back from beta to stable keeps the installed one
// until a newer stable release is out (019).
std::optional<UpdateRelease> ChooseUpdate(
    const std::vector<UpdateRelease>& releases,
    std::string_view current_version,
    std::string_view channel);

}  // namespace brouk

#endif  // BROUK_CORE_UPDATE_FEED_H_
