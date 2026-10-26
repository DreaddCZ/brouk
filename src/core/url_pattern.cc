#include "core/url_pattern.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace brouk {

namespace {

char Lower(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string ToLower(std::string_view text) {
  std::string out(text.size(), ' ');
  std::ranges::transform(text, out.begin(), Lower);
  return out;
}

bool IsHostChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
         c == '.' || c == '_';
}

struct ParsedUrl {
  std::string host;
  int port = 0;
  std::string path;
  std::string fragment;
};

// Scheme, host, port and path of an http(s) or ws(s) URL.
std::optional<ParsedUrl> ParseUrl(std::string_view url) {
  std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return std::nullopt;
  }
  std::string scheme = ToLower(url.substr(0, scheme_end));
  int default_port = 0;
  if (scheme == "http" || scheme == "ws") {
    default_port = 80;
  } else if (scheme == "https" || scheme == "wss") {
    default_port = 443;
  } else {
    return std::nullopt;
  }
  std::string_view rest = url.substr(scheme_end + 3);
  std::size_t authority_end = rest.find_first_of("/?#");
  std::string_view authority = rest.substr(0, authority_end);
  if (std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
    authority.remove_prefix(at + 1);  // user:password@
  }
  ParsedUrl parsed;
  parsed.port = default_port;
  std::size_t colon = authority.rfind(':');
  if (colon != std::string_view::npos &&
      authority.find(']') == std::string_view::npos) {
    std::string_view port = authority.substr(colon + 1);
    int value = 0;
    if (!port.empty() &&
        std::from_chars(port.data(), port.data() + port.size(), value).ec ==
            std::errc()) {
      parsed.port = value;
    }
    authority = authority.substr(0, colon);
  }
  parsed.host = ToLower(authority);
  std::string_view path = authority_end == std::string_view::npos
                              ? "/"
                              : rest.substr(authority_end);
  if (path.empty() || path.front() != '/') {
    path = "/";
  }
  parsed.path = std::string(path.substr(0, path.find_first_of("?#")));
  if (std::size_t hash = rest.find('#'); hash != std::string_view::npos) {
    parsed.fragment = std::string(rest.substr(hash + 1));
  }
  return parsed;
}

}  // namespace

std::optional<UrlPattern> UrlPattern::Parse(std::string_view text,
                                            Fragment fragment) {
  // Leading and trailing spaces are forgiven; a scheme is ignored.
  while (!text.empty() && text.front() == ' ')
    text.remove_prefix(1);
  while (!text.empty() && text.back() == ' ')
    text.remove_suffix(1);
  if (std::size_t scheme = text.find("://"); scheme != std::string_view::npos) {
    text.remove_prefix(scheme + 3);
  }
  UrlPattern pattern;
  if (text.starts_with("*.")) {
    pattern.subdomains = true;
    text.remove_prefix(2);
  }
  std::size_t slash = text.find('/');
  std::string_view authority = text.substr(0, slash);
  if (slash != std::string_view::npos) {
    std::string_view path = text.substr(slash);
    if (std::size_t hash = path.find('#'); hash != std::string_view::npos) {
      if (fragment != Fragment::kAllowed) {
        return std::nullopt;
      }
      pattern.fragment_prefix = std::string(path.substr(hash + 1));
      path = path.substr(0, hash);
    }
    pattern.path_prefix = std::string(path);
    // "github.com/company-a/*" reads naturally; the "*" adds nothing to a
    // prefix.
    std::string& last = pattern.fragment_prefix.empty()
                            ? pattern.path_prefix
                            : pattern.fragment_prefix;
    if (last.ends_with("/*")) {
      last.pop_back();
    }
    if (pattern.path_prefix.find_first_of(" ?") != std::string::npos ||
        pattern.fragment_prefix.find(' ') != std::string::npos) {
      return std::nullopt;
    }
  }
  if (std::size_t colon = authority.find(':');
      colon != std::string_view::npos) {
    std::string_view port = authority.substr(colon + 1);
    int value = 0;
    if (port.empty() ||
        std::from_chars(port.data(), port.data() + port.size(), value).ec !=
            std::errc() ||
        value < 1 || value > 65535) {
      return std::nullopt;
    }
    pattern.port = value;
    authority = authority.substr(0, colon);
  }
  pattern.host = ToLower(authority);
  if (pattern.host.empty() || pattern.host.size() > 253 ||
      !std::ranges::all_of(pattern.host, IsHostChar) ||
      pattern.host.front() == '.' || pattern.host.back() == '.') {
    return std::nullopt;
  }
  return pattern;
}

std::string UrlPattern::ToString() const {
  std::string text = subdomains ? "*." + host : host;
  if (port) {
    text += ":" + std::to_string(*port);
  }
  text += path_prefix;
  if (!fragment_prefix.empty()) {
    text += "#" + fragment_prefix;
  }
  return text;
}

bool UrlPattern::Matches(std::string_view url) const {
  std::optional<ParsedUrl> parsed = ParseUrl(url);
  if (!parsed) {
    return false;
  }
  bool host_matches =
      parsed->host == host ||
      (subdomains && parsed->host.size() > host.size() &&
       parsed->host.ends_with(host) &&
       parsed->host[parsed->host.size() - host.size() - 1] == '.');
  return host_matches && (!port || *port == parsed->port) &&
         parsed->path.starts_with(path_prefix) &&
         parsed->fragment.starts_with(fragment_prefix);
}

int UrlPattern::Specificity() const {
  // Exact host beats wildcard; then port; then the longer path (a fragment
  // continues it).
  return (subdomains ? 0 : 100000) + (port ? 10000 : 0) +
         static_cast<int>(std::min<std::size_t>(
             path_prefix.size() + fragment_prefix.size(), 9999));
}

std::optional<std::vector<UrlPattern>> ParseUrlPatternList(
    std::string_view text) {
  std::vector<UrlPattern> patterns;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = text.find(',', start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    std::string_view item = text.substr(start, end - start);
    while (!item.empty() && item.front() == ' ') {
      item.remove_prefix(1);
    }
    while (!item.empty() && item.back() == ' ') {
      item.remove_suffix(1);
    }
    if (!item.empty()) {
      std::optional<UrlPattern> pattern = UrlPattern::Parse(item);
      if (!pattern) {
        return std::nullopt;
      }
      patterns.push_back(std::move(*pattern));
    }
    start = end + 1;
  }
  return patterns;
}

bool MatchesAnyPattern(std::span<const UrlPattern> patterns,
                       std::string_view url) {
  return std::ranges::any_of(patterns, [url](const UrlPattern& pattern) {
    return pattern.Matches(url);
  });
}

}  // namespace brouk
