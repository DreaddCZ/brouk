#ifndef BROUK_CORE_LINK_ROUTING_RULES_H_
#define BROUK_CORE_LINK_ROUTING_RULES_H_

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/profile_id.h"
#include "core/result.h"
#include "core/yaml_value.h"

namespace brouk {

// A routing rule (010): links matching `pattern` open in one of `profiles`.
// One profile opens the link there at once; several show the picker, in
// this order.
struct RoutingRule {
  std::string pattern;  // UrlPattern text.
  std::vector<ProfileId> profiles;

  bool operator==(const RoutingRule& other) const = default;
};

// A link wrapper the user added (010): links to `pattern` are security
// redirects, and the real URL is in the query parameter `parameter`; an
// empty parameter means the real URL is only known after loading (opaque).
struct LinkWrapperRule {
  std::string pattern;  // UrlPattern text.
  std::string parameter;

  bool operator==(const LinkWrapperRule& other) const = default;
};

// Global, in Brouk/link-routing.yaml: a list of structured entries, which
// the flat settings file (004) cannot hold.
struct LinkRoutingConfig {
  std::vector<RoutingRule> rules;
  std::vector<LinkWrapperRule> wrappers;

  bool operator==(const LinkRoutingConfig& other) const = default;
};

// Why an entry cannot be used, or nullopt. Profiles are checked for their
// form only: a rule naming a deleted profile stays, and routing skips it.
std::optional<std::string> ValidateRoutingRule(const RoutingRule& rule);
std::optional<std::string> ValidateLinkWrapperRule(
    const LinkWrapperRule& wrapper);

// Invalid entries are left out and reported in `problems`.
LinkRoutingConfig LinkRoutingFromYaml(const YamlNode& root,
                                      std::vector<std::string>& problems);
YamlNode LinkRoutingToYaml(const LinkRoutingConfig& config);
// A missing file gives no rules.
LinkRoutingConfig LoadLinkRouting(const std::filesystem::path& path,
                                  std::vector<std::string>& problems);
std::optional<Error> SaveLinkRouting(const std::filesystem::path& path,
                                     const LinkRoutingConfig& config);

// The most specific rule matching `url`, or null: a longer path prefix
// first (any path beats none), then an exact host over a wildcard, then the
// longer host, then a port.
const RoutingRule* FindRoutingRule(const LinkRoutingConfig& config,
                                   std::string_view url);

// "Always open links matching … in this profile": the rule with `pattern`
// now lists only `profile`. Returns false if there is no such rule.
bool NarrowRoutingRule(LinkRoutingConfig& config,
                       std::string_view pattern,
                       const ProfileId& profile);

// The profile manager edits one profile's links (010): the patterns of the
// rules that list `profile`, in the rules' order.
std::vector<std::string> PatternsOfProfile(const LinkRoutingConfig& config,
                                           const ProfileId& profile);

// Makes `patterns` the profile's links. A rule for a pattern that is no
// longer listed loses the profile (and goes when it has no profile left);
// a listed pattern joins the rule for the same pattern (written in any
// equivalent way) at its end, or gets a new rule of its own. Patterns must
// be valid (UrlPattern::Parse).
void SetProfilePatterns(LinkRoutingConfig& config,
                        const ProfileId& profile,
                        std::span<const std::string> patterns);

}  // namespace brouk

#endif  // BROUK_CORE_LINK_ROUTING_RULES_H_
