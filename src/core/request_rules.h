#ifndef BROUK_CORE_REQUEST_RULES_H_
#define BROUK_CORE_REQUEST_RULES_H_

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/request_log.h"
#include "core/result.h"
#include "core/url_pattern.h"
#include "core/yaml_value.h"

namespace brouk {

// Request rules of a profile (006): change requests of tabs with dev tools
// on. Stored in profiles/<id>/request-rules.yaml.
struct RequestRule {
  enum class Action {
    kSetRequestHeader,  // Add or replace.
    kRemoveRequestHeader,
    kSetResponseHeader,
    kRemoveResponseHeader,
    kRedirect,
    kBlock,
  };
  std::string pattern;  // UrlPattern text.
  Action action = Action::kSetRequestHeader;
  std::string header;  // Header actions.
  std::string value;   // kSet*Header: the value; kRedirect: the URL.
  bool enabled = true;
};

struct RequestRuleSet {
  bool paused = false;
  std::vector<RequestRule> rules;
};

std::string_view RequestRuleActionName(RequestRule::Action action);
std::optional<RequestRule::Action> RequestRuleActionFromName(
    std::string_view name);

// Why a rule cannot be used, or nullopt.
std::optional<std::string> ValidateRequestRule(const RequestRule& rule);

// The rules file. Invalid rules are left out and reported in `problems`.
RequestRuleSet RequestRulesFromYaml(const YamlNode& root,
                                    std::vector<std::string>& problems);
YamlNode RequestRulesToYaml(const RequestRuleSet& set);
// A missing file gives no rules.
RequestRuleSet LoadRequestRules(const std::filesystem::path& path,
                                std::vector<std::string>& problems);
std::optional<Error> SaveRequestRules(const std::filesystem::path& path,
                                      const RequestRuleSet& set);

// What the rules do to one request. Header changes of all matching rules
// apply, the more specific ones last (so they win); of redirects and
// blocks, the most specific matching rule decides.
struct RequestRuleDecision {
  bool block = false;
  std::optional<std::string> redirect;
  bool changes_request_headers = false;
  bool changes_response_headers = false;
  bool affected() const {
    return block || redirect || changes_request_headers ||
           changes_response_headers;
  }
};

RequestRuleDecision DecideRequest(const RequestRuleSet& set,
                                  std::string_view url);
// The request headers after the rules for `url`.
Headers ApplyRequestHeaderRules(const RequestRuleSet& set,
                                std::string_view url,
                                Headers headers);
Headers ApplyResponseHeaderRules(const RequestRuleSet& set,
                                 std::string_view url,
                                 Headers headers);
// Response header rules cannot change a response Chromium loads: CEF hands
// the page the headers it got from the network. So Brouk loads requests
// that response header rules apply to itself and answers them with the
// changed headers (spec 006 notes). The whole response is kept in memory
// first, up to this size.
inline constexpr std::size_t kMaxOwnResponseBytes =
    std::size_t{64} * 1024 * 1024;

// Whether Brouk can load a request itself: not event streams or
// connection upgrades, which never end.
bool CanLoadForResponseRules(const Headers& request_headers);
// The page's request headers for Brouk's own request: without those the
// network stack sets itself (Host, Content-Length, Connection, ...) and
// without Referer, which is passed separately.
Headers HeadersForOwnRequest(const Headers& headers);
// The headers of a response Brouk loaded itself, for the page: the body is
// complete and already decoded, so without Content-Encoding,
// Content-Length and Transfer-Encoding.
Headers HeadersForLoadedResponse(const Headers& headers);
bool HasActiveRules(const RequestRuleSet& set);

}  // namespace brouk

#endif  // BROUK_CORE_REQUEST_RULES_H_
