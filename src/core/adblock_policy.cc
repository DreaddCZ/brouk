#include "core/adblock_policy.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace brouk {

namespace {

std::string Lower(std::string_view text) {
  std::string lower(text);
  for (char& c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return lower;
}

// The four numbers of an IPv4 address, or nullopt.
std::optional<std::array<int, 4>> Ipv4(std::string_view host) {
  std::array<int, 4> parts{};
  std::size_t pos = 0;
  for (int i = 0; i < 4; ++i) {
    std::size_t end = host.find('.', pos);
    if ((i < 3) != (end != std::string_view::npos)) {
      return std::nullopt;
    }
    std::string_view part =
        host.substr(pos, end == std::string_view::npos ? std::string_view::npos
                                                       : end - pos);
    auto [ptr, ec] =
        std::from_chars(part.data(), part.data() + part.size(), parts[i]);
    if (part.empty() || ec != std::errc() || ptr != part.data() + part.size() ||
        parts[i] < 0 || parts[i] > 255) {
      return std::nullopt;
    }
    pos = end + 1;
  }
  return parts;
}

std::vector<std::string_view> Labels(std::string_view host) {
  std::vector<std::string_view> labels;
  std::size_t pos = 0;
  while (pos <= host.size()) {
    std::size_t end = host.find('.', pos);
    labels.push_back(host.substr(pos, end == std::string_view::npos
                                          ? std::string_view::npos
                                          : end - pos));
    if (end == std::string_view::npos) {
      break;
    }
    pos = end + 1;
  }
  return labels;
}

std::string Site(std::string_view host) {
  if (Ipv4(host) || host.find(':') != std::string_view::npos) {
    return std::string(host);
  }
  std::vector<std::string_view> labels = Labels(host);
  std::size_t keep = 2;
  // co.uk, com.au, …: a short second-level label under a country code.
  if (labels.size() >= 3 && labels.back().size() == 2 &&
      labels[labels.size() - 2].size() <= 3) {
    keep = 3;
  }
  if (labels.size() <= keep) {
    return std::string(host);
  }
  std::string site;
  for (std::size_t i = labels.size() - keep; i < labels.size(); ++i) {
    site += (site.empty() ? "" : ".");
    site += labels[i];
  }
  return site;
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

std::optional<std::string> DecodeBase64(std::string_view text) {
  std::string out;
  int buffer = 0;
  int bits = 0;
  for (char c : text) {
    int value = 0;
    if (c >= 'A' && c <= 'Z') {
      value = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
      value = c - 'a' + 26;
    } else if (c >= '0' && c <= '9') {
      value = c - '0' + 52;
    } else if (c == '+' || c == '-') {
      value = 62;
    } else if (c == '/' || c == '_') {
      value = 63;
    } else if (c == '=' || c == '\n' || c == '\r') {
      continue;
    } else {
      return std::nullopt;
    }
    buffer = (buffer << 6) | value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buffer >> bits) & 0xFF);
    }
  }
  return out;
}

}  // namespace

std::string HostOfUrl(std::string_view url) {
  std::size_t scheme = url.find("://");
  if (scheme == std::string_view::npos) {
    return "";
  }
  std::string_view rest = url.substr(scheme + 3);
  rest = rest.substr(0, rest.find_first_of("/?#"));
  std::size_t at = rest.rfind('@');
  if (at != std::string_view::npos) {
    rest = rest.substr(at + 1);
  }
  if (rest.starts_with('[')) {
    std::size_t close = rest.find(']');
    return Lower(rest.substr(0, close == std::string_view::npos
                                    ? std::string_view::npos
                                    : close + 1));
  }
  return Lower(rest.substr(0, rest.find(':')));
}

bool IsProtectedHost(std::string_view host,
                     std::span<const std::string> extra) {
  std::string lower = Lower(host);
  if (lower.ends_with('.')) {
    lower.pop_back();
  }
  if (lower == "localhost" || lower.ends_with(".localhost") ||
      lower.ends_with(".local") || lower == "[::1]" || lower == "::1") {
    return true;
  }
  if (std::optional<std::array<int, 4>> ip = Ipv4(lower)) {
    auto [a, b, c, d] = *ip;
    (void)c;
    (void)d;
    return a == 127 || a == 10 || (a == 172 && b >= 16 && b <= 31) ||
           (a == 192 && b == 168) || (a == 169 && b == 254);
  }
  // IPv6 unique local (fc00::/7) and link-local (fe80::/10).
  if (lower.starts_with("[fc") || lower.starts_with("[fd") ||
      lower.starts_with("[fe8") || lower.starts_with("[fe9") ||
      lower.starts_with("[fea") || lower.starts_with("[feb")) {
    return true;
  }
  return std::ranges::any_of(extra, [&](const std::string& item) {
    std::string protected_host = Lower(item);
    return lower == protected_host || lower.ends_with("." + protected_host);
  });
}

bool IsThirdParty(std::string_view request_host, std::string_view page_host) {
  if (request_host.empty() || page_host.empty()) {
    return false;
  }
  return Site(Lower(request_host)) != Site(Lower(page_host));
}

AdblockDecision DecideRequest(const AdblockMatch& match,
                              std::string_view request_host,
                              std::span<const std::string> protected_extra) {
  AdblockDecision decision;
  decision.excepted = match.exception.has_value() && !match.block;
  if (match.block) {
    bool names_host = match.filter && !request_host.empty() &&
                      Lower(match.filter->text).find(Lower(request_host)) !=
                          std::string::npos;
    if (IsProtectedHost(request_host, protected_extra) && !names_host) {
      decision.excepted = true;
    } else {
      decision.block = true;
      decision.redirect = match.redirect;
      return decision;
    }
  }
  decision.rewritten_url = match.rewritten_url;
  return decision;
}

std::optional<DataUrl> ParseDataUrl(std::string_view url) {
  if (!url.starts_with("data:")) {
    return std::nullopt;
  }
  std::size_t comma = url.find(',');
  if (comma == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view meta = url.substr(5, comma - 5);
  std::string_view data = url.substr(comma + 1);
  bool base64 = meta.ends_with(";base64");
  if (base64) {
    meta.remove_suffix(7);
  }
  DataUrl result;
  result.mime_type = std::string(meta.substr(0, meta.find(';')));
  if (result.mime_type.empty()) {
    result.mime_type = "text/plain";
  }
  if (base64) {
    std::optional<std::string> body = DecodeBase64(data);
    if (!body) {
      return std::nullopt;
    }
    result.body = std::move(*body);
    return result;
  }
  // Percent-encoded.
  for (std::size_t i = 0; i < data.size(); ++i) {
    if (data[i] == '%' && i + 2 < data.size() && HexValue(data[i + 1]) >= 0 &&
        HexValue(data[i + 2]) >= 0) {
      result.body +=
          static_cast<char>(HexValue(data[i + 1]) * 16 + HexValue(data[i + 2]));
      i += 2;
    } else {
      result.body += data[i];
    }
  }
  return result;
}

}  // namespace brouk
