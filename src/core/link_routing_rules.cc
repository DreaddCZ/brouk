#include "core/link_routing_rules.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "core/url_pattern.h"
#include "core/yaml_io.h"

namespace brouk {

namespace {

const YamlNode* ScalarField(const YamlNode& map, std::string_view key) {
  const YamlNode* node = map.Find(key);
  return node && node->kind == YamlNode::Kind::kScalar ? node : nullptr;
}

bool IsParameterChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

// Higher is more specific (see FindRoutingRule).
auto RoutingSpecificity(const UrlPattern& pattern) {
  return std::make_tuple(pattern.path_prefix.size(),
                         pattern.fragment_prefix.size(), !pattern.subdomains,
                         pattern.host.size(), pattern.port.has_value());
}

std::vector<RoutingRule> RulesFromYaml(const YamlNode& node,
                                       std::vector<std::string>& problems) {
  std::vector<RoutingRule> rules;
  if (node.kind == YamlNode::Kind::kNull) {
    return rules;
  }
  if (node.kind != YamlNode::Kind::kSequence) {
    problems.push_back("line " + std::to_string(node.line) +
                       ": rules must be a list");
    return rules;
  }
  for (const YamlNode& item : node.items) {
    std::string where = "line " + std::to_string(item.line) + ": ";
    const YamlNode* pattern = item.kind == YamlNode::Kind::kMap
                                  ? ScalarField(item, "pattern")
                                  : nullptr;
    const YamlNode* profiles =
        item.kind == YamlNode::Kind::kMap ? item.Find("profiles") : nullptr;
    if (!pattern || !profiles || profiles->kind != YamlNode::Kind::kSequence) {
      problems.push_back(where +
                         "a rule needs a pattern and a list of profiles");
      continue;
    }
    RoutingRule rule;
    rule.pattern = pattern->scalar;
    bool scalars = true;
    for (const YamlNode& profile : profiles->items) {
      scalars = scalars && profile.kind == YamlNode::Kind::kScalar;
      rule.profiles.push_back(profile.scalar);
    }
    std::optional<std::string> problem =
        scalars ? ValidateRoutingRule(rule)
                : std::optional<std::string>("profiles must be profile ids");
    if (problem) {
      problems.push_back(where + *problem);
      continue;
    }
    rules.push_back(std::move(rule));
  }
  return rules;
}

std::vector<LinkWrapperRule> WrappersFromYaml(
    const YamlNode& node,
    std::vector<std::string>& problems) {
  std::vector<LinkWrapperRule> wrappers;
  if (node.kind == YamlNode::Kind::kNull) {
    return wrappers;
  }
  if (node.kind != YamlNode::Kind::kSequence) {
    problems.push_back("line " + std::to_string(node.line) +
                       ": wrappers must be a list");
    return wrappers;
  }
  for (const YamlNode& item : node.items) {
    std::string where = "line " + std::to_string(item.line) + ": ";
    const YamlNode* pattern = item.kind == YamlNode::Kind::kMap
                                  ? ScalarField(item, "pattern")
                                  : nullptr;
    const YamlNode* parameter =
        pattern ? ScalarField(item, "parameter") : nullptr;
    const YamlNode* opaque = pattern ? ScalarField(item, "opaque") : nullptr;
    bool is_opaque = opaque && opaque->plain && opaque->scalar == "true";
    if (!pattern || (parameter != nullptr) == is_opaque) {
      problems.push_back(where +
                         "a wrapper needs a pattern and either a parameter "
                         "or \"opaque: true\"");
      continue;
    }
    LinkWrapperRule wrapper{pattern->scalar,
                            parameter ? parameter->scalar : std::string()};
    if (std::optional<std::string> problem = ValidateLinkWrapperRule(wrapper)) {
      problems.push_back(where + *problem);
      continue;
    }
    wrappers.push_back(std::move(wrapper));
  }
  return wrappers;
}

}  // namespace

std::optional<std::string> ValidateRoutingRule(const RoutingRule& rule) {
  if (!UrlPattern::Parse(rule.pattern, UrlPattern::Fragment::kAllowed)) {
    return "\"" + rule.pattern +
           "\" is not a valid pattern (e.g. example.com, *.example.com or "
           "example.com/path/)";
  }
  if (rule.profiles.empty()) {
    return "a rule needs at least one profile";
  }
  for (auto it = rule.profiles.begin(); it != rule.profiles.end(); ++it) {
    if (!IsValidProfileId(*it)) {
      return "\"" + *it + "\" is not a profile id";
    }
    if (std::find(rule.profiles.begin(), it, *it) != it) {
      return "a rule lists a profile only once";
    }
  }
  return std::nullopt;
}

std::optional<std::string> ValidateLinkWrapperRule(
    const LinkWrapperRule& wrapper) {
  if (!UrlPattern::Parse(wrapper.pattern)) {
    return "\"" + wrapper.pattern + "\" is not a valid pattern";
  }
  if (wrapper.parameter.size() > 64 ||
      !std::ranges::all_of(wrapper.parameter, IsParameterChar)) {
    return "a parameter name has only letters, digits, '_', '-' and '.'";
  }
  return std::nullopt;
}

LinkRoutingConfig LinkRoutingFromYaml(const YamlNode& root,
                                      std::vector<std::string>& problems) {
  LinkRoutingConfig config;
  if (root.kind == YamlNode::Kind::kNull) {
    return config;
  }
  if (root.kind != YamlNode::Kind::kMap) {
    problems.emplace_back(
        R"(The link routing file must contain "rules:" and "wrappers:".)");
    return config;
  }
  if (const YamlNode* rules = root.Find("rules")) {
    config.rules = RulesFromYaml(*rules, problems);
  }
  if (const YamlNode* wrappers = root.Find("wrappers")) {
    config.wrappers = WrappersFromYaml(*wrappers, problems);
  }
  return config;
}

YamlNode LinkRoutingToYaml(const LinkRoutingConfig& config) {
  YamlNode root = YamlNode::Map();
  YamlNode& rules = root.GetOrAdd("rules");
  rules.kind = YamlNode::Kind::kSequence;
  for (const RoutingRule& rule : config.rules) {
    YamlNode item = YamlNode::Map();
    item.GetOrAdd("pattern") = YamlNode::Scalar(rule.pattern, false);
    YamlNode& profiles = item.GetOrAdd("profiles");
    profiles.kind = YamlNode::Kind::kSequence;
    for (const ProfileId& profile : rule.profiles) {
      profiles.items.push_back(YamlNode::Scalar(profile, false));
    }
    rules.items.push_back(std::move(item));
  }
  YamlNode& wrappers = root.GetOrAdd("wrappers");
  wrappers.kind = YamlNode::Kind::kSequence;
  for (const LinkWrapperRule& wrapper : config.wrappers) {
    YamlNode item = YamlNode::Map();
    item.GetOrAdd("pattern") = YamlNode::Scalar(wrapper.pattern, false);
    if (wrapper.parameter.empty()) {
      item.GetOrAdd("opaque") = YamlNode::Scalar("true", true);
    } else {
      item.GetOrAdd("parameter") = YamlNode::Scalar(wrapper.parameter, false);
    }
    wrappers.items.push_back(std::move(item));
  }
  return root;
}

LinkRoutingConfig LoadLinkRouting(const std::filesystem::path& path,
                                  std::vector<std::string>& problems) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    return {};
  }
  std::ifstream file(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  Result<YamlNode> root = ParseYaml(text);
  if (!root) {
    problems.push_back(root.error().message);
    return {};
  }
  return LinkRoutingFromYaml(root.value(), problems);
}

std::optional<Error> SaveLinkRouting(const std::filesystem::path& path,
                                     const LinkRoutingConfig& config) {
  std::string yaml = EmitYaml(LinkRoutingToYaml(config));
  if (yaml.empty()) {
    return Error{"The link routing rules could not be written."};
  }
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file << "# Brouk link routing: which profile opens which links, and the "
            "link\n# wrappers to look through. Profiles are named by their "
            "id. You can\n# edit this file while Brouk runs; comments are not "
            "kept.\n"
         << yaml;
    if (!file) {
      return Error{"The link routing rules could not be written."};
    }
  }
  std::filesystem::rename(temp, path, error);
  if (error) {
    return Error{"The link routing rules could not be saved: " +
                 error.message()};
  }
  return std::nullopt;
}

const RoutingRule* FindRoutingRule(const LinkRoutingConfig& config,
                                   std::string_view url) {
  const RoutingRule* best = nullptr;
  std::optional<UrlPattern> best_pattern;
  for (const RoutingRule& rule : config.rules) {
    std::optional<UrlPattern> pattern =
        UrlPattern::Parse(rule.pattern, UrlPattern::Fragment::kAllowed);
    if (!pattern || !pattern->Matches(url)) {
      continue;
    }
    // The first of equally specific rules wins.
    if (!best_pattern ||
        RoutingSpecificity(*pattern) > RoutingSpecificity(*best_pattern)) {
      best = &rule;
      best_pattern = std::move(pattern);
    }
  }
  return best;
}

namespace {

// The same pattern however it was written ("https://Example.com/a/*" and
// "example.com/a/").
std::string NormalPattern(std::string_view pattern) {
  std::optional<UrlPattern> parsed =
      UrlPattern::Parse(pattern, UrlPattern::Fragment::kAllowed);
  return parsed ? parsed->ToString() : std::string(pattern);
}

}  // namespace

std::vector<std::string> PatternsOfProfile(const LinkRoutingConfig& config,
                                           const ProfileId& profile) {
  std::vector<std::string> patterns;
  for (const RoutingRule& rule : config.rules) {
    if (std::ranges::find(rule.profiles, profile) != rule.profiles.end()) {
      patterns.push_back(rule.pattern);
    }
  }
  return patterns;
}

void SetProfilePatterns(LinkRoutingConfig& config,
                        const ProfileId& profile,
                        std::span<const std::string> patterns) {
  std::vector<std::string> wanted;
  for (const std::string& pattern : patterns) {
    std::string normal = NormalPattern(pattern);
    if (std::ranges::find(wanted, normal) == wanted.end()) {
      wanted.push_back(std::move(normal));
    }
  }
  for (RoutingRule& rule : config.rules) {
    bool listed =
        std::ranges::find(wanted, NormalPattern(rule.pattern)) != wanted.end();
    auto it = std::ranges::find(rule.profiles, profile);
    if (!listed && it != rule.profiles.end()) {
      rule.profiles.erase(it);
    } else if (listed && it == rule.profiles.end()) {
      rule.profiles.push_back(profile);
    }
  }
  std::erase_if(config.rules,
                [](const RoutingRule& rule) { return rule.profiles.empty(); });
  for (const std::string& pattern : wanted) {
    bool exists = std::ranges::any_of(config.rules, [&](const RoutingRule& r) {
      return NormalPattern(r.pattern) == pattern;
    });
    if (!exists) {
      config.rules.push_back({pattern, {profile}});
    }
  }
}

bool NarrowRoutingRule(LinkRoutingConfig& config,
                       std::string_view pattern,
                       const ProfileId& profile) {
  for (RoutingRule& rule : config.rules) {
    if (rule.pattern == pattern) {
      rule.profiles = {profile};
      return true;
    }
  }
  return false;
}

}  // namespace brouk
