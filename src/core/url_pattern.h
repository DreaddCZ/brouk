#ifndef BROUK_CORE_URL_PATTERN_H_
#define BROUK_CORE_URL_PATTERN_H_

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace brouk {

// URL patterns shared by request rules (006) and link routing (010):
//   example.com              that host (any scheme and port)
//   *.example.com            its subdomains and the host itself
//   example.com:8443         that host and port
//   example.com/api/         that host, paths starting with /api/
//   example.com/api/*        the same
//   example.com/#@tenant/*   also fragments starting with @tenant/ (only
//                            where allowed: link routing, for Azure tenants)
// Hosts are compared without case. A pattern that is more specific wins:
// an exact host over a wildcard, a longer path prefix over a shorter one.
struct UrlPattern {
  // Requests never carry a fragment, so only link routing matches it.
  enum class Fragment { kNotAllowed, kAllowed };

  std::string host;         // Lower case, without "*.".
  bool subdomains = false;  // "*." prefix.
  std::optional<int> port;
  std::string path_prefix;      // "" or starting with "/".
  std::string fragment_prefix;  // After "#"; "" matches any fragment.

  // nullopt for text that is not a valid pattern.
  static std::optional<UrlPattern> Parse(
      std::string_view text,
      Fragment fragment = Fragment::kNotAllowed);
  std::string ToString() const;

  bool Matches(std::string_view url) const;
  // Higher is more specific.
  int Specificity() const;
};

// A comma-separated list of patterns, as a setting holds it (013: never
// record these sites); "" is an empty list. nullopt if any is not valid.
std::optional<std::vector<UrlPattern>> ParseUrlPatternList(
    std::string_view text);
bool MatchesAnyPattern(std::span<const UrlPattern> patterns,
                       std::string_view url);

}  // namespace brouk

#endif  // BROUK_CORE_URL_PATTERN_H_
