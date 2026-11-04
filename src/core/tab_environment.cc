#include "core/tab_environment.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>

namespace brouk {

namespace {

constexpr std::size_t kMaxLanguages = 10;
constexpr std::size_t kMaxUserAgentBytes = 512;
constexpr std::size_t kMaxLabelBytes = 64;

bool IsAlpha(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool IsAlnum(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

// "en", "de-DE", "zh-Hant-TW": a language and subtags (BCP 47, simplified).
bool IsLanguageTag(std::string_view tag) {
  std::size_t start = 0;
  bool first = true;
  while (start <= tag.size()) {
    std::size_t end = tag.find('-', start);
    if (end == std::string_view::npos) {
      end = tag.size();
    }
    std::string_view part = tag.substr(start, end - start);
    bool valid = first ? part.size() >= 2 && part.size() <= 3 &&
                             std::ranges::all_of(part, IsAlpha)
                       : part.size() >= 2 && part.size() <= 8 &&
                             std::ranges::all_of(part, IsAlnum);
    if (!valid) {
      return false;
    }
    first = false;
    start = end + 1;
    if (end == tag.size()) {
      break;
    }
  }
  return !tag.empty();
}

// "Asia/Tokyo", "America/Argentina/Buenos_Aires", "Etc/GMT+5", "UTC".
bool IsTimeZoneName(std::string_view name) {
  return !name.empty() && name.size() <= 64 &&
         std::ranges::all_of(name, [](char c) {
           return IsAlnum(c) || c == '/' || c == '_' || c == '-' || c == '+';
         });
}

bool IsPrintableAscii(std::string_view text) {
  return std::ranges::all_of(text,
                             [](char c) { return c >= 0x20 && c < 0x7f; });
}

}  // namespace

bool TabEnvironment::empty() const {
  return time_zone.empty() && languages.empty() && !location &&
         user_agent_preset.empty() && user_agent.empty() &&
         color_scheme.empty() && !reduced_motion;
}

std::optional<std::string> TabEnvironmentProblem(const TabEnvironment& env) {
  if (!env.time_zone.empty() && !IsTimeZoneName(env.time_zone)) {
    return "Not a time zone name.";
  }
  if (env.languages.size() > kMaxLanguages) {
    return "At most 10 languages.";
  }
  for (const std::string& language : env.languages) {
    if (!IsLanguageTag(language)) {
      return "\"" + language.substr(0, 20) +
             "\" is not a language tag (such as de-DE or en).";
    }
  }
  if (env.location && !env.location->unavailable) {
    const TabEnvironment::Location& at = *env.location;
    if (!std::isfinite(at.latitude) || std::abs(at.latitude) > 90 ||
        !std::isfinite(at.longitude) || std::abs(at.longitude) > 180 ||
        !std::isfinite(at.accuracy) || at.accuracy <= 0 || at.accuracy > 1e6) {
      return "Latitude is -90 to 90, longitude -180 to 180, accuracy above "
             "0 metres.";
    }
    if (at.label.size() > kMaxLabelBytes) {
      return "The location's name is too long.";
    }
  }
  if (!env.user_agent_preset.empty() &&
      !FindUserAgentPreset(env.user_agent_preset)) {
    return "Unknown user agent preset.";
  }
  if (!env.user_agent_preset.empty() && !env.user_agent.empty()) {
    return "Either a user agent preset or custom text.";
  }
  if (env.user_agent.size() > kMaxUserAgentBytes ||
      !IsPrintableAscii(env.user_agent)) {
    return "The user agent must be plain text of at most 512 characters.";
  }
  if (!env.color_scheme.empty() && env.color_scheme != "light" &&
      env.color_scheme != "dark") {
    return "The colour scheme is light or dark.";
  }
  return std::nullopt;
}

const std::vector<UserAgentPreset>& UserAgentPresets() {
  // Reduced user agents, as current browsers send them. Safari's and
  // Firefox's versions are fixed here (they are not Chromium).
  static const std::vector<UserAgentPreset> kPresets = {
      {"chrome-windows", "Chrome on Windows",
       "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
       "like Gecko) Chrome/{v}.0.0.0 Safari/537.36",
       "Win32", "Google Chrome", "Windows", "15.0.0", false},
      {"chrome-mac", "Chrome on macOS",
       "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
       "(KHTML, like Gecko) Chrome/{v}.0.0.0 Safari/537.36",
       "MacIntel", "Google Chrome", "macOS", "15.0.0", false},
      {"chrome-android", "Chrome on Android",
       "Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like "
       "Gecko) Chrome/{v}.0.0.0 Mobile Safari/537.36",
       "Linux armv81", "Google Chrome", "Android", "14.0.0", true},
      {"chrome-iphone", "Chrome on iPhone",
       "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) "
       "AppleWebKit/605.1.15 (KHTML, like Gecko) CriOS/{v}.0.0.0 "
       "Mobile/15E148 Safari/604.1",
       "iPhone", "", "", "", true},
      {"edge-windows", "Edge on Windows",
       "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
       "like Gecko) Chrome/{v}.0.0.0 Safari/537.36 Edg/{v}.0.0.0",
       "Win32", "Microsoft Edge", "Windows", "15.0.0", false},
      {"safari-mac", "Safari on macOS",
       "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 "
       "(KHTML, like Gecko) Version/18.0 Safari/605.1.15",
       "MacIntel", "", "", "", false},
      {"safari-iphone", "Safari on iPhone",
       "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) "
       "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 "
       "Safari/604.1",
       "iPhone", "", "", "", true},
      {"firefox-windows", "Firefox on Windows",
       "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:140.0) Gecko/20100101 "
       "Firefox/140.0",
       "Win32", "", "", "", false},
  };
  return kPresets;
}

const UserAgentPreset* FindUserAgentPreset(std::string_view id) {
  const std::vector<UserAgentPreset>& presets = UserAgentPresets();
  auto it = std::ranges::find(presets, id, &UserAgentPreset::id);
  return it == presets.end() ? nullptr : &*it;
}

std::string PresetUserAgent(const UserAgentPreset& preset, int chrome_major) {
  std::string text(preset.user_agent);
  const std::string version = std::to_string(chrome_major);
  for (std::size_t at = text.find("{v}"); at != std::string::npos;
       at = text.find("{v}", at)) {
    text.replace(at, 3, version);
    at += version.size();
  }
  return text;
}

std::string TabEnvironmentSummary(const TabEnvironment& env) {
  std::vector<std::string> parts;
  if (!env.time_zone.empty()) {
    parts.push_back("time zone " + env.time_zone);
  }
  if (!env.languages.empty()) {
    std::string list;
    for (const std::string& language : env.languages) {
      list += (list.empty() ? "" : ", ") + language;
    }
    parts.push_back("language " + list);
  }
  if (env.location) {
    if (env.location->unavailable) {
      parts.emplace_back("location unavailable");
    } else if (!env.location->label.empty()) {
      parts.push_back("location " + env.location->label);
    } else {
      std::array<char, 64> text{};
      std::snprintf(text.data(), text.size(), "location %.4f, %.4f",
                    env.location->latitude, env.location->longitude);
      parts.emplace_back(text.data());
    }
  }
  if (const UserAgentPreset* preset =
          FindUserAgentPreset(env.user_agent_preset)) {
    parts.push_back("user agent " + std::string(preset->label));
  } else if (!env.user_agent.empty()) {
    parts.emplace_back("custom user agent");
  }
  if (!env.color_scheme.empty()) {
    parts.push_back(env.color_scheme + " colour scheme");
  }
  if (env.reduced_motion) {
    parts.emplace_back("reduced motion");
  }
  std::string summary;
  for (const std::string& part : parts) {
    summary += (summary.empty() ? "" : ", ") + part;
  }
  return summary;
}

}  // namespace brouk
