#ifndef BROUK_CORE_WEB_ORIGIN_H_
#define BROUK_CORE_WEB_ORIGIN_H_

#include <optional>
#include <string>
#include <string_view>

namespace brouk {

// Origins and sites, for matching saved passwords to pages (008). URLs come
// canonicalised from Chromium (lower-case scheme and host, punycode), so
// simple parsing is enough here.

// "https://example.com" or "http://localhost:8080" (default ports left
// out); nullopt for URLs that are not http(s) or have no host. User names,
// paths, queries and fragments are dropped.
std::optional<std::string> WebOriginOf(std::string_view url);

// The parts of an origin from WebOriginOf.
std::string SchemeOfOrigin(std::string_view origin);
std::string HostOfOrigin(std::string_view origin);

// The site of a host: its registrable domain from the public suffix list
// ("example.co.uk" for "login.example.co.uk"), or the host itself for IP
// addresses, single-label names and public suffixes.
std::string SiteOfHost(std::string_view host);

// Whether a login saved for `entry_origin` is offered on `page_origin` as
// another host of the same site: same site, and the same scheme or an http
// entry on an https page (never the other way round, as in Chrome).
bool IsSameSiteOrUpgrade(std::string_view entry_origin,
                         std::string_view page_origin);

}  // namespace brouk

#endif  // BROUK_CORE_WEB_ORIGIN_H_
