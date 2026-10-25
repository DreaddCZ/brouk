#include "core/link_unwrap.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/url_pattern.h"

namespace brouk {

namespace {

constexpr int kMaxWrapperDepth = 5;
constexpr std::size_t kMaxUrlLength = std::size_t{32} * 1024;

char Lower(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string ToLower(std::string_view text) {
  std::string out(text.size(), ' ');
  std::ranges::transform(text, out.begin(), Lower);
  return out;
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  c = Lower(c);
  return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// %XX sequences; anything else (also '+') stays as it is.
std::string PercentDecode(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size() && HexValue(text[i + 1]) >= 0 &&
        HexValue(text[i + 2]) >= 0) {
      out +=
          static_cast<char>(HexValue(text[i + 1]) * 16 + HexValue(text[i + 2]));
      i += 2;
    } else {
      out += text[i];
    }
  }
  return out;
}

// The few entities Proofpoint's own decoder undoes.
std::string HtmlUnescape(std::string_view text) {
  static constexpr std::array<std::pair<std::string_view, char>, 6> kEntities =
      {{{"&amp;", '&'},
        {"&lt;", '<'},
        {"&gt;", '>'},
        {"&quot;", '"'},
        {"&#39;", '\''},
        {"&#x27;", '\''}}};
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    bool replaced = false;
    if (text[i] == '&') {
      for (const auto& [entity, c] : kEntities) {
        if (text.substr(i).starts_with(entity)) {
          out += c;
          i += entity.size() - 1;
          replaced = true;
          break;
        }
      }
    }
    if (!replaced) {
      out += text[i];
    }
  }
  return out;
}

struct LinkParts {
  std::string host;   // Lower case, without user info and port.
  std::string path;   // Raw, up to '?' or '#'.
  std::string query;  // Raw, without '?', up to '#'.
  std::string rest;   // Raw, everything after the host and port.
};

std::optional<LinkParts> SplitLink(std::string_view url) {
  std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return std::nullopt;
  }
  std::string scheme = ToLower(url.substr(0, scheme_end));
  if (scheme != "http" && scheme != "https") {
    return std::nullopt;
  }
  std::string_view after = url.substr(scheme_end + 3);
  std::size_t authority_end = after.find_first_of("/?#");
  std::string_view authority = after.substr(0, authority_end);
  if (std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
    authority.remove_prefix(at + 1);
  }
  if (std::size_t colon = authority.rfind(':');
      colon != std::string_view::npos &&
      authority.find(']') == std::string_view::npos) {
    authority = authority.substr(0, colon);
  }
  LinkParts parts;
  parts.host = ToLower(authority);
  if (parts.host.empty()) {
    return std::nullopt;
  }
  if (authority_end == std::string_view::npos) {
    parts.path = "/";
    return parts;
  }
  std::string_view rest = after.substr(authority_end);
  parts.rest = std::string(rest);
  std::string_view without_fragment = rest.substr(0, rest.find('#'));
  std::size_t question = without_fragment.find('?');
  parts.path = std::string(without_fragment.substr(0, question));
  if (parts.path.empty()) {
    parts.path = "/";
  }
  if (question != std::string_view::npos) {
    parts.query = std::string(without_fragment.substr(question + 1));
  }
  return parts;
}

// The decoded value of the first query parameter `name`.
std::optional<std::string> QueryParameter(std::string_view query,
                                          std::string_view name) {
  std::size_t start = 0;
  while (start <= query.size()) {
    std::size_t end = query.find('&', start);
    if (end == std::string_view::npos) {
      end = query.size();
    }
    std::string_view pair = query.substr(start, end - start);
    std::size_t equals = pair.find('=');
    if (equals != std::string_view::npos && pair.substr(0, equals) == name) {
      return PercentDecode(pair.substr(equals + 1));
    }
    start = end + 1;
  }
  return std::nullopt;
}

bool HostIs(const LinkParts& parts, std::string_view host) {
  return parts.host == host;
}

bool HostEndsWith(const LinkParts& parts, std::string_view suffix) {
  return parts.host.size() > suffix.size() && parts.host.ends_with(suffix);
}

// URL-safe base64 (RFC 4648 §5), padding optional; nullopt if malformed.
std::optional<std::string> Base64UrlDecode(std::string_view text) {
  while (!text.empty() && text.back() == '=') {
    text.remove_suffix(1);
  }
  std::string out;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (char c : text) {
    int value = -1;
    if (c >= 'A' && c <= 'Z') {
      value = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
      value = c - 'a' + 26;
    } else if (c >= '0' && c <= '9') {
      value = c - '0' + 52;
    } else if (c == '-' || c == '+') {
      value = 62;
    } else if (c == '_' || c == '/') {
      value = 63;
    } else {
      return std::nullopt;
    }
    buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buffer >> bits) & 0xff);
    }
  }
  return out;
}

// Splits UTF-8 text into its characters (each as its bytes); nullopt if it
// is not valid UTF-8.
std::optional<std::vector<std::string>> Utf8Characters(std::string_view text) {
  std::vector<std::string> characters;
  std::size_t i = 0;
  while (i < text.size()) {
    auto byte = static_cast<unsigned char>(text[i]);
    std::size_t length = byte < 0x80             ? 1
                         : (byte & 0xe0) == 0xc0 ? 2
                         : (byte & 0xf0) == 0xe0 ? 3
                         : (byte & 0xf8) == 0xf0 ? 4
                                                 : 0;
    if (length == 0 || i + length > text.size()) {
      return std::nullopt;
    }
    for (std::size_t k = 1; k < length; ++k) {
      if ((static_cast<unsigned char>(text[i + k]) & 0xc0) != 0x80) {
        return std::nullopt;
      }
    }
    characters.emplace_back(text.substr(i, length));
    i += length;
  }
  return characters;
}

// Proofpoint URL Defense v1: …/v1/url?u=<percent-encoded URL>&k=…
std::optional<std::string> DecodeProofpointV1(const LinkParts& parts) {
  std::optional<std::string> u = QueryParameter(parts.query, "u");
  return u ? std::optional(HtmlUnescape(*u)) : std::nullopt;
}

// v2: …/v2/url?u=<URL percent-encoded with '-' for '%' and '_' for '/'>&d=…
std::optional<std::string> DecodeProofpointV2(const LinkParts& parts) {
  std::size_t start = 0;
  std::string_view query = parts.query;
  while (start <= query.size()) {
    std::size_t end = query.find('&', start);
    if (end == std::string_view::npos) {
      end = query.size();
    }
    std::string_view pair = query.substr(start, end - start);
    if (pair.starts_with("u=")) {
      std::string value(pair.substr(2));
      std::ranges::replace(value, '-', '%');
      std::ranges::replace(value, '_', '/');
      return HtmlUnescape(PercentDecode(value));
    }
    start = end + 1;
  }
  return std::nullopt;
}

// v3: …/v3/__<URL>__;<base64>!!… Each '*' in the URL stands for the next
// character of the base64 text, "**" plus a letter for a run of them
// (A = 2, B = 3, …), as in Proofpoint's published decoder.
std::optional<std::string> DecodeProofpointV3(const LinkParts& parts) {
  std::string_view rest = parts.rest;
  if (!rest.starts_with("/v3/__")) {
    return std::nullopt;
  }
  rest.remove_prefix(6);
  std::size_t url_end = rest.find("__;");
  if (url_end == std::string_view::npos) {
    return std::nullopt;
  }
  std::string url = PercentDecode(rest.substr(0, url_end));
  std::string_view encoded = rest.substr(url_end + 3);
  encoded = encoded.substr(0, encoded.find('!'));
  std::optional<std::string> bytes = Base64UrlDecode(encoded);
  std::optional<std::vector<std::string>> characters =
      bytes ? Utf8Characters(*bytes) : std::nullopt;
  if (!characters) {
    return std::nullopt;
  }
  static constexpr std::string_view kRunLetters =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  std::size_t next = 0;
  for (std::size_t i = 0; i < url.size(); ++i) {
    if (url[i] != '*') {
      out += url[i];
      continue;
    }
    std::size_t run = 1;
    if (i + 2 < url.size() && url[i + 1] == '*') {
      std::size_t letter = kRunLetters.find(url[i + 2]);
      if (letter == std::string_view::npos) {
        return std::nullopt;
      }
      run = letter + 2;
      i += 2;
    }
    if (next + run > characters->size()) {
      return std::nullopt;
    }
    for (std::size_t k = 0; k < run; ++k) {
      out += (*characters)[next++];
    }
  }
  return out;
}

std::optional<std::string> DecodeProofpoint(const LinkParts& parts) {
  if (parts.path.starts_with("/v1/")) {
    return DecodeProofpointV1(parts);
  }
  if (parts.path.starts_with("/v2/")) {
    return DecodeProofpointV2(parts);
  }
  if (parts.path.starts_with("/v3/")) {
    return DecodeProofpointV3(parts);
  }
  return std::nullopt;
}

std::optional<std::string> DecodeUrlParameter(const LinkParts& parts) {
  return QueryParameter(parts.query, "url");
}

std::optional<std::string> DecodeBarracuda(const LinkParts& parts) {
  return QueryParameter(parts.query, "a");
}

// Cisco Secure Email: secure-web.cisco.com/<token>/<percent-encoded URL>.
std::optional<std::string> DecodeCisco(const LinkParts& parts) {
  std::string_view rest = parts.rest;
  if (!rest.starts_with('/')) {
    return std::nullopt;
  }
  std::size_t slash = rest.find('/', 1);
  if (slash == std::string_view::npos) {
    return std::nullopt;
  }
  return PercentDecode(rest.substr(slash + 1));
}

std::optional<std::string> DecodeGoogle(const LinkParts& parts) {
  std::optional<std::string> q = QueryParameter(parts.query, "q");
  return q ? q : QueryParameter(parts.query, "url");
}

struct BuiltInWrapper {
  std::string_view name;
  bool (*matches)(const LinkParts& parts);
  // Null for opaque wrappers.
  std::optional<std::string> (*decode)(const LinkParts& parts);
};

constexpr std::array<BuiltInWrapper, 7> kBuiltInWrappers = {{
    {"Safe Links",
     [](const LinkParts& parts) {
       return HostEndsWith(parts, ".safelinks.protection.outlook.com");
     },
     DecodeUrlParameter},
    {"Safe Links",
     [](const LinkParts& parts) {
       return HostIs(parts, "statics.teams.cdn.office.net") &&
              parts.path.starts_with("/evergreen-assets/safelinks/");
     },
     DecodeUrlParameter},
    {"URL Defense",
     [](const LinkParts& parts) {
       return HostIs(parts, "urldefense.com") ||
              HostIs(parts, "urldefense.proofpoint.com");
     },
     DecodeProofpoint},
    {"Barracuda",
     [](const LinkParts& parts) {
       return HostIs(parts, "linkprotect.cudasvc.com");
     },
     DecodeBarracuda},
    {"Cisco Secure Email",
     [](const LinkParts& parts) {
       return HostIs(parts, "secure-web.cisco.com");
     },
     DecodeCisco},
    {"Google",
     [](const LinkParts& parts) {
       return (HostIs(parts, "www.google.com") ||
               HostIs(parts, "google.com")) &&
              parts.path == "/url";
     },
     DecodeGoogle},
    {"Mimecast",
     [](const LinkParts& parts) {
       return (parts.host.starts_with("protect") &&
               HostEndsWith(parts, ".mimecast.com")) ||
              HostIs(parts, "mimecastprotect.com") ||
              HostEndsWith(parts, ".mimecastprotect.com");
     },
     nullptr},
}};

struct WrapperMatch {
  std::string name;
  // nullopt: opaque, or the link could not be decoded.
  std::optional<std::string> decoded;
};

std::optional<WrapperMatch> MatchWrapper(
    std::string_view url,
    std::span<const LinkWrapperRule> user_wrappers) {
  std::optional<LinkParts> parts = SplitLink(url);
  if (!parts) {
    return std::nullopt;
  }
  for (const LinkWrapperRule& wrapper : user_wrappers) {
    std::optional<UrlPattern> pattern = UrlPattern::Parse(wrapper.pattern);
    if (!pattern || !pattern->Matches(url)) {
      continue;
    }
    WrapperMatch match{pattern->ToString(), std::nullopt};
    if (!wrapper.parameter.empty()) {
      match.decoded = QueryParameter(parts->query, wrapper.parameter);
    }
    return match;
  }
  for (const BuiltInWrapper& wrapper : kBuiltInWrappers) {
    if (wrapper.matches(*parts)) {
      return WrapperMatch{
          std::string(wrapper.name),
          wrapper.decode ? wrapper.decode(*parts) : std::nullopt};
    }
  }
  return std::nullopt;
}

bool IsHostByte(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
         c == '[' || c == ']' || c == ':' || c >= 0x80;
}

}  // namespace

bool IsRoutableUrl(std::string_view url) {
  if (url.size() > kMaxUrlLength || std::ranges::any_of(url, [](char c) {
        auto byte = static_cast<unsigned char>(c);
        return byte <= 0x20 || byte == 0x7f;
      })) {
    return false;
  }
  std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return false;
  }
  std::string scheme = ToLower(url.substr(0, scheme_end));
  if (scheme != "http" && scheme != "https") {
    return false;
  }
  std::string_view authority = url.substr(scheme_end + 3);
  authority = authority.substr(0, authority.find_first_of("/?#\\"));
  if (std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
    authority.remove_prefix(at + 1);
  }
  return !authority.empty() && authority.front() != ':' &&
         std::ranges::all_of(authority, [](char c) {
           return IsHostByte(static_cast<unsigned char>(c));
         });
}

UnwrappedLink UnwrapLink(std::string_view url,
                         std::span<const LinkWrapperRule> user_wrappers) {
  UnwrappedLink result;
  result.url = std::string(url);
  std::string current(url);
  for (int depth = 0; depth <= kMaxWrapperDepth; ++depth) {
    std::optional<WrapperMatch> match = MatchWrapper(current, user_wrappers);
    if (!match) {
      if (depth > 0) {
        result.kind = UnwrappedLink::Kind::kDecoded;
        result.url = current;
      }
      return result;
    }
    if (depth == 0) {
      result.wrapper = match->name;
    }
    if (depth == kMaxWrapperDepth || !match->decoded ||
        !IsRoutableUrl(*match->decoded)) {
      break;
    }
    current = std::move(*match->decoded);
  }
  // Opaque: the link itself is loaded and followed (010).
  result.kind = UnwrappedLink::Kind::kOpaque;
  result.url = std::string(url);
  return result;
}

std::optional<std::string> LinkWrapperName(
    std::string_view url,
    std::span<const LinkWrapperRule> user_wrappers) {
  std::optional<WrapperMatch> match = MatchWrapper(url, user_wrappers);
  return match ? std::optional(match->name) : std::nullopt;
}

}  // namespace brouk
