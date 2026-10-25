#ifndef BROUK_CORE_LINK_ROUTER_H_
#define BROUK_CORE_LINK_ROUTER_H_

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/link_routing_rules.h"
#include "core/profile_id.h"

namespace brouk {

// Where a routed link comes from (010). Navigation inside a tab, redirects
// and popups opened by scripts are never routed, so they have no source
// here.
enum class LinkSource {
  kOtherApp,     // Another app opened the link.
  kNewTab,       // Cmd-click, "Open Link in New Tab", target=_blank, …
  kTypedNewTab,  // Typed into the address bar of a new, empty tab.
};

// Which profile opens a link.
struct ProfileDecision {
  // One: open there. Several: the picker offers them, in the rule's order.
  std::vector<ProfileId> profiles;
  // The rule that matched, for "Always open links matching … in this
  // profile"; nullopt when no rule decided.
  std::optional<std::string> rule_pattern;
  // The profile is the source tab's own one: the link stays where it was
  // opened (next to its opener for kNewTab).
  bool stays_in_source = false;
};

// `url` is the real URL (after UnwrapLink). `source_profile` is the
// profile of the tab the link came from (unused for kOtherApp).
// `existing` lists the profiles that exist; rules naming deleted profiles
// skip them, and a rule left with none counts as no match.
//
// No match: a link from inside the browser stays in its profile; one from
// another app opens in the default profile. A rule listing the source
// profile among several keeps the link there without asking (user's
// decision, 2026-10-25).
ProfileDecision DecideProfile(const LinkRoutingConfig& config,
                              std::string_view url,
                              LinkSource source,
                              const ProfileId& source_profile,
                              const ProfileId& default_profile,
                              std::span<const ProfileId> existing);

// A browser window, as routing needs to know it.
struct RoutingWindow {
  std::int64_t id = 0;
  // Profiles with a group in the window (the default profile's group is
  // always there, 003).
  std::vector<ProfileId> profiles;
  bool on_current_screen = false;
  bool on_active_space = false;
};

// Which window opens a link, once its profile is known.
struct WindowDecision {
  enum class Kind {
    kWindow,     // In `window`, in the profile's group.
    kChoose,     // The picker offers `choices` (and "New window").
    kNewGroup,   // In `window`, as a new group of the profile.
    kNewWindow,  // Brouk has no window.
  };
  Kind kind = Kind::kNewWindow;
  std::int64_t window = 0;
  std::vector<std::int64_t> choices;  // Most recently used first.
};

// `windows` are the normal browser windows, most recently used first.
//   Default profile: the most recently used window on the current screen
//   (preferring the active Space), else the most recently used one
//   anywhere (user's decision, 2026-10-25).
//   Other profiles: its group in one window → there; in several → the
//   user chooses; in none → a new group in the frontmost window.
WindowDecision DecideWindow(const ProfileId& profile,
                            const ProfileId& default_profile,
                            std::span<const RoutingWindow> windows);

}  // namespace brouk

#endif  // BROUK_CORE_LINK_ROUTER_H_
