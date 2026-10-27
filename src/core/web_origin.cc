#include "core/web_origin.h"

#include <algorithm>
#include <cctype>

#include "brouk_adblock.h"

namespace brouk {

namespace {

std::string Lower(std::string_view text) {
  std::string out(text);
  std::ranges::transform(out, out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

}  // namespace

std::optional<std::string> WebOriginOf(std::string_view url) {
  std::size_t separator = url.find("://");
  if (separator == std::string_view::npos) {
    return std::nullopt;
  }
  std::string scheme = Lower(url.substr(0, separator));
  if (scheme != "http" && scheme != "https") {
    return std::nullopt;
  }
  std::string_view authority = url.substr(separator + 3);
  authority = authority.substr(0, authority.find_first_of("/?#"));
  std::size_t at = authority.rfind('@');
  if (at != std::string_view::npos) {
    authority = authority.substr(at + 1);
  }
  std::string_view host = authority;
  std::string_view port;
  std::size_t colon = authority.rfind(':');
  std::size_t bracket = authority.rfind(']');
  if (colon != std::string_view::npos &&
      (bracket == std::string_view::npos || colon > bracket)) {
    host = authority.substr(0, colon);
    port = authority.substr(colon + 1);
  }
  if (host.empty() ||
      !std::ranges::all_of(port, [](char c) { return c >= '0' && c <= '9'; })) {
    return std::nullopt;
  }
  if ((scheme == "https" && port == "443") ||
      (scheme == "http" && port == "80")) {
    port = {};
  }
  std::string origin = scheme + "://" + Lower(host);
  if (!port.empty()) {
    origin += ':';
    origin += port;
  }
  return origin;
}

std::string SchemeOfOrigin(std::string_view origin) {
  std::size_t separator = origin.find("://");
  return separator == std::string_view::npos
             ? std::string()
             : std::string(origin.substr(0, separator));
}

std::string HostOfOrigin(std::string_view origin) {
  std::size_t separator = origin.find("://");
  if (separator == std::string_view::npos) {
    return {};
  }
  std::string_view host = origin.substr(separator + 3);
  std::size_t colon = host.rfind(':');
  std::size_t bracket = host.rfind(']');
  if (colon != std::string_view::npos &&
      (bracket == std::string_view::npos || colon > bracket)) {
    host = host.substr(0, colon);
  }
  return std::string(host);
}

std::string SiteOfHost(std::string_view host) {
  std::string lower = Lower(host);
  // IPv6 and IPv4 addresses are their own site (the list would treat the
  // last number of an IPv4 address as a top-level domain).
  if (lower.empty() || lower.front() == '[' ||
      std::ranges::all_of(
          lower, [](char c) { return (c >= '0' && c <= '9') || c == '.'; })) {
    return lower;
  }
  char* domain = brouk_registrable_domain(lower.c_str());
  if (domain == nullptr) {
    return lower;
  }
  std::string site(domain);
  brouk_adblock_free_string(domain);
  return site;
}

}  // namespace brouk
