#ifndef BROUK_CORE_TAB_ENVIRONMENT_H_
#define BROUK_CORE_TAB_ENVIRONMENT_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brouk {

// What a tab sees instead of the computer's settings while its dev tools
// are on (027, Tab environment): to test how a site behaves elsewhere.
// Applied through the tab's Chrome DevTools Protocol session.
struct TabEnvironment {
  struct Location {
    double latitude = 0;
    double longitude = 0;
    double accuracy = 150;  // Metres.
    std::string label;      // "Tokyo"; empty for typed coordinates.
    bool unavailable = false;
  };

  std::string time_zone;               // IANA name; empty: not changed.
  std::vector<std::string> languages;  // In order; empty: not changed.
  std::optional<Location> location;
  std::string user_agent_preset;  // An id of UserAgentPresets().
  std::string user_agent;         // Custom text (when no preset).
  std::string color_scheme;       // "light", "dark" or empty.
  bool reduced_motion = false;

  bool empty() const;
};

// Why the environment cannot be applied, or nullopt. Values come from a
// page (the tools page), so everything is checked.
std::optional<std::string> TabEnvironmentProblem(const TabEnvironment& env);

// A user agent the tab can pretend to be.
struct UserAgentPreset {
  std::string_view id;
  std::string_view label;
  // "{v}" stands for the Chrome major version.
  std::string_view user_agent;
  // navigator.platform.
  std::string_view platform;
  // User agent client hints (Sec-CH-UA…); empty brand: none are sent, as
  // Safari and Firefox send none.
  std::string_view brand;  // "Google Chrome", "Microsoft Edge" or empty.
  std::string_view hint_platform;
  std::string_view hint_platform_version;
  bool mobile = false;
};

const std::vector<UserAgentPreset>& UserAgentPresets();
const UserAgentPreset* FindUserAgentPreset(std::string_view id);
std::string PresetUserAgent(const UserAgentPreset& preset, int chrome_major);

// "time zone Asia/Tokyo, language de-DE, location Tokyo" for the dock's
// bar; empty when nothing is changed.
std::string TabEnvironmentSummary(const TabEnvironment& env);

}  // namespace brouk

#endif  // BROUK_CORE_TAB_ENVIRONMENT_H_
