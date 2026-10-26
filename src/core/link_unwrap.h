#ifndef BROUK_CORE_LINK_UNWRAP_H_
#define BROUK_CORE_LINK_UNWRAP_H_

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/link_routing_rules.h"

namespace brouk {

// What a link really points to, looking through security wrappers (010:
// Safe Links, URL Defense, Mimecast, …), so routing uses the real URL.
struct UnwrappedLink {
  enum class Kind {
    kPlain,    // Not a wrapper: `url` is the link itself.
    kDecoded,  // `url` is the real URL, read from the link.
    kOpaque,   // The real URL is only known after loading the link.
  };
  Kind kind = Kind::kPlain;
  // kPlain and kDecoded: the URL to route by; kOpaque: the link itself.
  std::string url;
  // The outermost wrapper's name for "via …" ("Safe Links"); empty for
  // kPlain.
  std::string wrapper;
};

// Unwraps up to 5 nested wrappers. The user's wrappers are checked before
// the built-in ones. A decoded URL that is not a valid http(s) URL, or a
// sixth wrapper, makes the link opaque: Brouk then never routes by text it
// cannot trust.
UnwrappedLink UnwrapLink(std::string_view url,
                         std::span<const LinkWrapperRule> user_wrappers);

// The wrapper's name if `url` is on a wrapper's host (decodable or opaque):
// resolving an opaque link stops as soon as it leaves wrapper hosts.
std::optional<std::string> LinkWrapperName(
    std::string_view url,
    std::span<const LinkWrapperRule> user_wrappers);

// For the log, to see why a link was routed as it was: host, the start of
// the path and the query parameters' names, never their values (tokens).
std::string LinkForLog(std::string_view url);

// An absolute http or https URL with a host and no spaces or control
// characters.
bool IsRoutableUrl(std::string_view url);

// The built-in wrappers, for the settings page.
struct BuiltInLinkWrapper {
  std::string_view name;
  std::string_view hosts;  // As people read them.
  bool opaque = false;
};
std::span<const BuiltInLinkWrapper> BuiltInLinkWrappers();

}  // namespace brouk

#endif  // BROUK_CORE_LINK_UNWRAP_H_
