#include "core/link_router.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace brouk {

namespace {

bool Contains(std::span<const ProfileId> profiles, std::string_view profile) {
  return std::ranges::find(profiles, profile) != profiles.end();
}

}  // namespace

ProfileDecision DecideProfile(const LinkRoutingConfig& config,
                              std::string_view url,
                              LinkSource source,
                              const ProfileId& source_profile,
                              const ProfileId& default_profile,
                              std::span<const ProfileId> existing) {
  bool from_browser = source != LinkSource::kOtherApp;
  ProfileDecision decision;
  if (const RoutingRule* rule = FindRoutingRule(config, url)) {
    for (const ProfileId& profile : rule->profiles) {
      if (Contains(existing, profile)) {
        decision.profiles.push_back(profile);
      }
    }
    if (!decision.profiles.empty()) {
      decision.rule_pattern = rule->pattern;
      if (from_browser && Contains(decision.profiles, source_profile)) {
        decision.profiles = {source_profile};
        decision.stays_in_source = true;
      }
      return decision;
    }
  }
  decision.profiles = {from_browser ? source_profile : default_profile};
  decision.stays_in_source = from_browser;
  return decision;
}

WindowDecision DecideWindow(const ProfileId& profile,
                            const ProfileId& default_profile,
                            std::span<const RoutingWindow> windows) {
  WindowDecision decision;
  if (windows.empty()) {
    return decision;  // kNewWindow.
  }
  if (profile == default_profile) {
    auto here = [](const RoutingWindow& window) {
      return window.on_current_screen && window.on_active_space;
    };
    auto on_screen = [](const RoutingWindow& window) {
      return window.on_current_screen;
    };
    auto it = std::ranges::find_if(windows, here);
    if (it == windows.end()) {
      it = std::ranges::find_if(windows, on_screen);
    }
    decision.kind = WindowDecision::Kind::kWindow;
    decision.window = it != windows.end() ? it->id : windows.front().id;
    return decision;
  }
  for (const RoutingWindow& window : windows) {
    if (Contains(window.profiles, profile)) {
      decision.choices.push_back(window.id);
    }
  }
  if (decision.choices.empty()) {
    decision.kind = WindowDecision::Kind::kNewGroup;
    decision.window = windows.front().id;
  } else if (decision.choices.size() == 1) {
    decision.kind = WindowDecision::Kind::kWindow;
    decision.window = decision.choices.front();
    decision.choices.clear();
  } else {
    decision.kind = WindowDecision::Kind::kChoose;
  }
  return decision;
}

}  // namespace brouk
