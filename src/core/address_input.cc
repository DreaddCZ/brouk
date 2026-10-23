#include "core/address_input.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace brouk {

namespace {

// Schemes typed in the address bar that load (or hand off) as written.
// Unknown "word:rest" text without "//" is a search, as in Chrome
// ("site:example.com" searches). External protocols such as "mailto:" are
// listed so that typing them still asks to open the external app (012).
// "javascript:" is deliberately missing: like Chrome's paste protection, it
// keeps pasted scripts from running in the current page.
constexpr std::array<std::string_view, 15> kKnownSchemes = {
    "http",  "https",       "file",     "about",  "data", "blob", "chrome",
    "ftp",   "view-source", "devtools", "mailto", "tel",  "ws",   "wss",
    "brouk",  // Internal pages (brouk://history).
};

bool IsAsciiAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool IsAsciiDigit(char c) {
  return c >= '0' && c <= '9';
}

char ToLowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string ToLowerAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    c = ToLowerAscii(c);
  }
  return out;
}

bool IsWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && IsWhitespace(s.front())) {
    s.remove_prefix(1);
  }
  while (!s.empty() && IsWhitespace(s.back())) {
    s.remove_suffix(1);
  }
  return s;
}

bool ContainsWhitespace(std::string_view s) {
  for (char c : s) {
    if (IsWhitespace(c)) {
      return true;
    }
  }
  return false;
}

// Returns the scheme if `s` starts with "scheme:" (RFC 3986 scheme syntax).
std::string_view LeadingScheme(std::string_view s) {
  if (s.empty() || !IsAsciiAlpha(s.front())) {
    return {};
  }
  for (std::size_t i = 1; i < s.size(); ++i) {
    char c = s[i];
    if (c == ':') {
      return s.substr(0, i);
    }
    if (!IsAsciiAlpha(c) && !IsAsciiDigit(c) && c != '+' && c != '-' &&
        c != '.') {
      return {};
    }
  }
  return {};
}

bool IsKnownScheme(std::string_view scheme) {
  std::string lower = ToLowerAscii(scheme);
  for (std::string_view known : kKnownSchemes) {
    if (lower == known) {
      return true;
    }
  }
  return false;
}

bool IsIPv4(std::string_view host) {
  int parts = 0;
  while (true) {
    std::size_t dot = host.find('.');
    std::string_view part = host.substr(0, dot);
    if (part.empty() || part.size() > 3) {
      return false;
    }
    int value = 0;
    for (char c : part) {
      if (!IsAsciiDigit(c)) {
        return false;
      }
      value = value * 10 + (c - '0');
    }
    if (value > 255) {
      return false;
    }
    ++parts;
    if (dot == std::string_view::npos) {
      break;
    }
    host.remove_prefix(dot + 1);
  }
  return parts == 4;
}

bool IsBracketedIPv6(std::string_view host) {
  if (host.size() < 4 || host.front() != '[' || host.back() != ']') {
    return false;
  }
  bool has_colon = false;
  for (char c : host.substr(1, host.size() - 2)) {
    if (c == ':') {
      has_colon = true;
    } else if (!std::isxdigit(static_cast<unsigned char>(c)) && c != '.') {
      return false;
    }
  }
  return has_colon;
}

bool IsHostLabel(std::string_view label) {
  if (label.empty() || label.size() > 63 || label.front() == '-' ||
      label.back() == '-') {
    return false;
  }
  for (char c : label) {
    // Non-ASCII bytes are allowed: internationalized domain names are
    // converted to punycode by Chromium when the URL is loaded.
    bool ok = IsAsciiAlpha(c) || IsAsciiDigit(c) || c == '-' ||
              static_cast<unsigned char>(c) >= 0x80;
    if (!ok) {
      return false;
    }
  }
  return true;
}

// A top-level domain must look like one: letters (or non-ASCII letters, or
// punycode "xn--"), at least two characters. This keeps "1.5" or "file.c3"
// out, while Chrome's full public-suffix check is not needed for daily use.
bool IsPlausibleTld(std::string_view tld) {
  if (tld.size() < 2) {
    return false;
  }
  if (tld.size() > 4 && ToLowerAscii(tld.substr(0, 4)) == "xn--") {
    return true;
  }
  for (char c : tld) {
    if (!IsAsciiAlpha(c) && static_cast<unsigned char>(c) < 0x80) {
      return false;
    }
  }
  return true;
}

enum class HostKind { kNotAHost, kDomain, kLocal };

// Classifies the host part of "host[:port]" text typed without a scheme.
HostKind ClassifyHost(std::string_view host) {
  if (host.empty()) {
    return HostKind::kNotAHost;
  }
  if (IsBracketedIPv6(host)) {
    return HostKind::kLocal;
  }
  if (host.back() == '.') {
    host.remove_suffix(1);  // "example.com." is a valid fully-qualified name.
  }
  if (ToLowerAscii(host) == "localhost" || IsIPv4(host)) {
    return HostKind::kLocal;
  }
  std::size_t last_dot = host.rfind('.');
  if (last_dot == std::string_view::npos) {
    return HostKind::kNotAHost;  // Single word: search (Chrome does too).
  }
  std::string_view rest = host;
  while (true) {
    std::size_t dot = rest.find('.');
    if (!IsHostLabel(rest.substr(0, dot))) {
      return HostKind::kNotAHost;
    }
    if (dot == std::string_view::npos) {
      break;
    }
    rest.remove_prefix(dot + 1);
  }
  return IsPlausibleTld(host.substr(last_dot + 1)) ? HostKind::kDomain
                                                   : HostKind::kNotAHost;
}

bool IsValidPort(std::string_view port) {
  if (port.empty() || port.size() > 5) {
    return false;
  }
  int value = 0;
  for (char c : port) {
    if (!IsAsciiDigit(c)) {
      return false;
    }
    value = value * 10 + (c - '0');
  }
  return value <= 65535;
}

// Returns the URL for "host[:port][/path][?query][#fragment]" text, or an
// empty string if the text does not look like one.
std::string UrlFromSchemelessInput(std::string_view input) {
  std::size_t authority_end = input.find_first_of("/?#");
  std::string_view authority = input.substr(0, authority_end);
  if (authority.find('@') != std::string_view::npos) {
    return {};  // "user@example.com" is more likely an e-mail: search.
  }
  std::string_view host = authority;
  // Split off ":port", but not inside a bracketed IPv6 address.
  std::size_t colon = authority.rfind(':');
  std::size_t bracket = authority.rfind(']');
  if (colon != std::string_view::npos &&
      (bracket == std::string_view::npos || colon > bracket)) {
    if (!IsValidPort(authority.substr(colon + 1))) {
      return {};
    }
    host = authority.substr(0, colon);
  }
  switch (ClassifyHost(host)) {
    case HostKind::kNotAHost:
      return {};
    case HostKind::kLocal:
      // Chrome does not upgrade localhost and IP addresses to https.
      return "http://" + std::string(input);
    case HostKind::kDomain:
      // Chrome upgrades typed navigations to https by default.
      return "https://" + std::string(input);
  }
  return {};
}

}  // namespace

std::string BuildSearchUrl(std::string_view query,
                           std::string_view search_template) {
  std::string url(search_template);
  std::size_t pos = url.find("%s");
  if (pos == std::string::npos) {
    return url;
  }
  url.replace(pos, 2, EncodeQueryComponent(query));
  return url;
}

std::string EncodeQueryComponent(std::string_view text) {
  static constexpr std::string_view kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size() * 3);
  for (char c : text) {
    auto byte = static_cast<unsigned char>(c);
    if (IsAsciiAlpha(c) || IsAsciiDigit(c) || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out.push_back(c);
    } else if (c == ' ') {
      out.push_back('+');
    } else {
      out.push_back('%');
      out.push_back(kHex[byte >> 4]);
      out.push_back(kHex[byte & 0x0F]);
    }
  }
  return out;
}

AddressInputResult ClassifyAddressInput(std::string_view input,
                                        std::string_view search_template) {
  std::string_view text = Trim(input);
  if (text.empty()) {
    return {};
  }

  if (!ContainsWhitespace(text)) {
    std::string_view scheme = LeadingScheme(text);
    if (!scheme.empty()) {
      std::string_view after = text.substr(scheme.size() + 1);
      bool has_slashes = after.starts_with("//");
      if (IsKnownScheme(scheme) || has_slashes) {
        return {AddressInputResult::Kind::kUrl, std::string(text)};
      }
    }
    std::string url = UrlFromSchemelessInput(text);
    if (!url.empty()) {
      return {AddressInputResult::Kind::kUrl, std::move(url)};
    }
  }

  return {AddressInputResult::Kind::kSearch,
          BuildSearchUrl(text, search_template)};
}

}  // namespace brouk
