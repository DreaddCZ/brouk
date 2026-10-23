#include "core/request_rules.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/yaml_io.h"

namespace brouk {

namespace {

constexpr std::array<std::pair<RequestRule::Action, std::string_view>, 6>
    kActions = {{
        {RequestRule::Action::kSetRequestHeader, "set-request-header"},
        {RequestRule::Action::kRemoveRequestHeader, "remove-request-header"},
        {RequestRule::Action::kSetResponseHeader, "set-response-header"},
        {RequestRule::Action::kRemoveResponseHeader, "remove-response-header"},
        {RequestRule::Action::kRedirect, "redirect"},
        {RequestRule::Action::kBlock, "block"},
    }};

bool IsHeaderAction(RequestRule::Action action) {
  return action != RequestRule::Action::kRedirect &&
         action != RequestRule::Action::kBlock;
}

bool IsSetAction(RequestRule::Action action) {
  return action == RequestRule::Action::kSetRequestHeader ||
         action == RequestRule::Action::kSetResponseHeader;
}

bool IsTokenChar(char c) {
  // RFC 9110 token characters.
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}

bool EqualsIgnoringCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) {
           auto lower = [](char c) {
             return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
           };
           return lower(x) == lower(y);
         });
}

// Matching enabled rules, least specific first.
std::vector<const RequestRule*> Matching(const RequestRuleSet& set,
                                         std::string_view url) {
  std::vector<std::pair<int, const RequestRule*>> found;
  if (set.paused) {
    return {};
  }
  for (const RequestRule& rule : set.rules) {
    if (!rule.enabled) {
      continue;
    }
    std::optional<UrlPattern> pattern = UrlPattern::Parse(rule.pattern);
    if (pattern && pattern->Matches(url)) {
      found.emplace_back(pattern->Specificity(), &rule);
    }
  }
  std::ranges::stable_sort(found, {},
                           &std::pair<int, const RequestRule*>::first);
  std::vector<const RequestRule*> rules;
  rules.reserve(found.size());
  for (const auto& [specificity, rule] : found) {
    rules.push_back(rule);
  }
  return rules;
}

Headers ApplyHeaderRules(const RequestRuleSet& set,
                         std::string_view url,
                         Headers headers,
                         RequestRule::Action set_action,
                         RequestRule::Action remove_action) {
  for (const RequestRule* rule : Matching(set, url)) {
    if (rule->action != set_action && rule->action != remove_action) {
      continue;
    }
    std::erase_if(headers, [rule](const Header& header) {
      return EqualsIgnoringCase(header.first, rule->header);
    });
    if (rule->action == set_action) {
      headers.emplace_back(rule->header, rule->value);
    }
  }
  return headers;
}

const YamlNode* ScalarField(const YamlNode& map, std::string_view key) {
  const YamlNode* node = map.Find(key);
  return node && node->kind == YamlNode::Kind::kScalar ? node : nullptr;
}

}  // namespace

std::string_view RequestRuleActionName(RequestRule::Action action) {
  for (const auto& [value, name] : kActions) {
    if (value == action) {
      return name;
    }
  }
  return "block";
}

std::optional<RequestRule::Action> RequestRuleActionFromName(
    std::string_view name) {
  for (const auto& [value, action_name] : kActions) {
    if (action_name == name) {
      return value;
    }
  }
  return std::nullopt;
}

std::optional<std::string> ValidateRequestRule(const RequestRule& rule) {
  if (!UrlPattern::Parse(rule.pattern)) {
    return "The URL pattern is not valid (e.g. example.com, *.example.com, "
           "example.com/api/).";
  }
  if (IsHeaderAction(rule.action)) {
    if (rule.header.empty() || rule.header.size() > 256 ||
        !std::ranges::all_of(rule.header, IsTokenChar)) {
      return std::string("The header name is not valid.");
    }
  }
  if (rule.value.size() > 8192 ||
      rule.value.find_first_of("\r\n") != std::string::npos ||
      rule.value.find('\0') != std::string::npos) {
    return std::string("The value must be a single line.");
  }
  if (rule.action == RequestRule::Action::kRedirect &&
      !rule.value.starts_with("https://") &&
      !rule.value.starts_with("http://")) {
    return std::string("The redirect target must be an http or https URL.");
  }
  return std::nullopt;
}

RequestRuleSet RequestRulesFromYaml(const YamlNode& root,
                                    std::vector<std::string>& problems) {
  RequestRuleSet set;
  if (root.kind == YamlNode::Kind::kNull) {
    return set;
  }
  if (root.kind != YamlNode::Kind::kMap) {
    problems.emplace_back("The rules file must contain \"rules:\".");
    return set;
  }
  if (const YamlNode* paused = ScalarField(root, "paused")) {
    if (paused->plain &&
        (paused->scalar == "true" || paused->scalar == "false")) {
      set.paused = paused->scalar == "true";
    } else {
      problems.push_back("line " + std::to_string(paused->line) +
                         ": paused must be true or false");
    }
  }
  const YamlNode* rules = root.Find("rules");
  if (!rules || rules->kind == YamlNode::Kind::kNull) {
    return set;
  }
  if (rules->kind != YamlNode::Kind::kSequence) {
    problems.push_back("line " + std::to_string(rules->line) +
                       ": rules must be a list");
    return set;
  }
  for (const YamlNode& item : rules->items) {
    std::string where = "line " + std::to_string(item.line) + ": ";
    if (item.kind != YamlNode::Kind::kMap) {
      problems.push_back(where +
                         "a rule must have fields (pattern, action, ...)");
      continue;
    }
    RequestRule rule;
    const YamlNode* pattern = ScalarField(item, "pattern");
    const YamlNode* action = ScalarField(item, "action");
    std::optional<RequestRule::Action> parsed =
        action ? RequestRuleActionFromName(action->scalar) : std::nullopt;
    if (!pattern || !parsed) {
      problems.push_back(where +
                         "a rule needs a pattern and an action (set-request-"
                         "header, remove-request-header, set-response-header, "
                         "remove-response-header, redirect, block)");
      continue;
    }
    rule.pattern = pattern->scalar;
    rule.action = *parsed;
    if (const YamlNode* header = ScalarField(item, "header")) {
      rule.header = header->scalar;
    }
    if (const YamlNode* value = ScalarField(item, "value")) {
      rule.value = value->scalar;
    } else if (const YamlNode* url = ScalarField(item, "url")) {
      rule.value = url->scalar;
    }
    if (const YamlNode* enabled = ScalarField(item, "enabled")) {
      rule.enabled = !(enabled->plain && enabled->scalar == "false");
    }
    if (std::optional<std::string> problem = ValidateRequestRule(rule)) {
      problems.push_back(where + *problem);
      continue;
    }
    set.rules.push_back(std::move(rule));
  }
  return set;
}

YamlNode RequestRulesToYaml(const RequestRuleSet& set) {
  YamlNode root = YamlNode::Map();
  root.GetOrAdd("paused") =
      YamlNode::Scalar(set.paused ? "true" : "false", true);
  YamlNode& rules = root.GetOrAdd("rules");
  rules.kind = YamlNode::Kind::kSequence;
  for (const RequestRule& rule : set.rules) {
    YamlNode item = YamlNode::Map();
    item.GetOrAdd("pattern") = YamlNode::Scalar(rule.pattern, false);
    item.GetOrAdd("action") =
        YamlNode::Scalar(std::string(RequestRuleActionName(rule.action)), true);
    if (IsHeaderAction(rule.action)) {
      item.GetOrAdd("header") = YamlNode::Scalar(rule.header, false);
    }
    if (IsSetAction(rule.action)) {
      item.GetOrAdd("value") = YamlNode::Scalar(rule.value, false);
    }
    if (rule.action == RequestRule::Action::kRedirect) {
      item.GetOrAdd("url") = YamlNode::Scalar(rule.value, false);
    }
    item.GetOrAdd("enabled") =
        YamlNode::Scalar(rule.enabled ? "true" : "false", true);
    rules.items.push_back(std::move(item));
  }
  return root;
}

RequestRuleSet LoadRequestRules(const std::filesystem::path& path,
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
  return RequestRulesFromYaml(root.value(), problems);
}

std::optional<Error> SaveRequestRules(const std::filesystem::path& path,
                                      const RequestRuleSet& set) {
  std::string yaml = EmitYaml(RequestRulesToYaml(set));
  if (yaml.empty()) {
    return Error{"The rules could not be written."};
  }
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file << "# Request rules of this profile (Brouk developer tools). They "
            "apply only\n# to tabs with developer tools on.\n"
         << yaml;
    if (!file) {
      return Error{"The rules could not be written."};
    }
  }
  std::filesystem::permissions(
      temp,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      error);
  std::filesystem::rename(temp, path, error);
  if (error) {
    return Error{"The rules could not be saved: " + error.message()};
  }
  return std::nullopt;
}

RequestRuleDecision DecideRequest(const RequestRuleSet& set,
                                  std::string_view url) {
  RequestRuleDecision decision;
  const RequestRule* routing = nullptr;  // Most specific redirect/block.
  for (const RequestRule* rule : Matching(set, url)) {
    switch (rule->action) {
      case RequestRule::Action::kSetRequestHeader:
      case RequestRule::Action::kRemoveRequestHeader:
        decision.changes_request_headers = true;
        break;
      case RequestRule::Action::kSetResponseHeader:
      case RequestRule::Action::kRemoveResponseHeader:
        decision.changes_response_headers = true;
        break;
      case RequestRule::Action::kRedirect:
      case RequestRule::Action::kBlock:
        routing = rule;  // Later ones are more specific.
        break;
    }
  }
  if (routing && routing->action == RequestRule::Action::kBlock) {
    decision.block = true;
  } else if (routing && routing->value != url) {
    decision.redirect = routing->value;
  }
  return decision;
}

Headers ApplyRequestHeaderRules(const RequestRuleSet& set,
                                std::string_view url,
                                Headers headers) {
  return ApplyHeaderRules(set, url, std::move(headers),
                          RequestRule::Action::kSetRequestHeader,
                          RequestRule::Action::kRemoveRequestHeader);
}

Headers ApplyResponseHeaderRules(const RequestRuleSet& set,
                                 std::string_view url,
                                 Headers headers) {
  return ApplyHeaderRules(set, url, std::move(headers),
                          RequestRule::Action::kSetResponseHeader,
                          RequestRule::Action::kRemoveResponseHeader);
}

bool HasResponseRules(const RequestRuleSet& set) {
  return !set.paused &&
         std::ranges::any_of(set.rules, [](const RequestRule& rule) {
           return rule.enabled &&
                  (rule.action == RequestRule::Action::kSetResponseHeader ||
                   rule.action == RequestRule::Action::kRemoveResponseHeader);
         });
}

bool HasActiveRules(const RequestRuleSet& set) {
  return !set.paused && std::ranges::any_of(set.rules, &RequestRule::enabled);
}

}  // namespace brouk
