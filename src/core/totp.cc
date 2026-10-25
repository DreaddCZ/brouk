#include "core/totp.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace brouk {

namespace {

constexpr std::string_view kScheme = "otpauth://totp/";
constexpr std::size_t kMinKeyBytes = 10;  // 80 bits (RFC 4226 minimum).

std::string Lower(std::string_view text) {
  std::string out(text);
  std::ranges::transform(out, out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
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

std::string PercentDecode(std::string_view text) {
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size() && HexValue(text[i + 1]) >= 0 &&
        HexValue(text[i + 2]) >= 0) {
      out.push_back(static_cast<char>((HexValue(text[i + 1]) * 16) +
                                      HexValue(text[i + 2])));
      i += 2;
    } else if (text[i] == '+') {
      out.push_back(' ');
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

std::string PercentEncode(std::string_view text) {
  constexpr std::string_view kDigits = "0123456789ABCDEF";
  std::string out;
  for (char c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (std::isalnum(byte) != 0 || c == '-' || c == '.' || c == '_' ||
        c == '~') {
      out.push_back(c);
    } else {
      out.push_back('%');
      out.push_back(kDigits[byte >> 4]);
      out.push_back(kDigits[byte & 0x0F]);
    }
  }
  return out;
}

std::optional<int> ParseInt(std::string_view text) {
  int value = 0;
  auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

std::string Base32Encode(std::string_view bytes) {
  constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  std::string out;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (char c : bytes) {
    buffer = (buffer << 8) | static_cast<unsigned char>(c);
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      out.push_back(kAlphabet[(buffer >> bits) & 0x1F]);
    }
  }
  if (bits > 0) {
    out.push_back(kAlphabet[(buffer << (5 - bits)) & 0x1F]);
  }
  return out;
}

std::optional<TotpSecret> FromBase32(std::string_view text) {
  std::optional<std::string> key = Base32Decode(text);
  if (!key || key->size() < kMinKeyBytes) {
    return std::nullopt;
  }
  TotpSecret secret;
  secret.key = std::move(*key);
  return secret;
}

std::optional<TotpSecret> FromUri(std::string_view uri) {
  std::string_view rest = uri.substr(kScheme.size());
  std::string_view label = rest.substr(0, rest.find('?'));
  std::string_view query =
      label.size() < rest.size() ? rest.substr(label.size() + 1) : "";
  std::optional<TotpSecret> secret;
  std::string issuer;
  int digits = 6;
  int period = 30;
  HmacAlgorithm algorithm = HmacAlgorithm::kSha1;
  while (!query.empty()) {
    std::string_view pair = query.substr(0, query.find('&'));
    query.remove_prefix(std::min(query.size(), pair.size() + 1));
    std::size_t equals = pair.find('=');
    if (equals == std::string_view::npos) {
      continue;
    }
    std::string name = Lower(pair.substr(0, equals));
    std::string value = PercentDecode(pair.substr(equals + 1));
    if (name == "secret") {
      secret = FromBase32(value);
      if (!secret) {
        return std::nullopt;
      }
    } else if (name == "issuer") {
      issuer = value;
    } else if (name == "digits") {
      std::optional<int> number = ParseInt(value);
      if (!number || *number < 6 || *number > 8) {
        return std::nullopt;
      }
      digits = *number;
    } else if (name == "period") {
      std::optional<int> number = ParseInt(value);
      if (!number || *number < 1 || *number > 300) {
        return std::nullopt;
      }
      period = *number;
    } else if (name == "algorithm") {
      std::string lower = Lower(value);
      if (lower == "sha1") {
        algorithm = HmacAlgorithm::kSha1;
      } else if (lower == "sha256") {
        algorithm = HmacAlgorithm::kSha256;
      } else if (lower == "sha512") {
        algorithm = HmacAlgorithm::kSha512;
      } else {
        return std::nullopt;
      }
    }
  }
  if (!secret) {
    return std::nullopt;
  }
  secret->algorithm = algorithm;
  secret->digits = digits;
  secret->period = period;
  // The label is "Issuer:account" or "account".
  std::string decoded = PercentDecode(label);
  std::size_t colon = decoded.find(':');
  if (colon != std::string::npos) {
    secret->account = decoded.substr(colon + 1);
    while (!secret->account.empty() && secret->account.front() == ' ') {
      secret->account.erase(0, 1);
    }
    if (issuer.empty()) {
      issuer = decoded.substr(0, colon);
    }
  } else {
    secret->account = decoded;
  }
  secret->issuer = issuer;
  return secret;
}

}  // namespace

std::optional<TotpSecret> ParseTotpSecret(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  if (Lower(text.substr(0, kScheme.size())) == kScheme) {
    return FromUri(text);
  }
  return FromBase32(text);
}

std::string TotpSecretToUri(const TotpSecret& secret) {
  std::string label = PercentEncode(secret.issuer);
  if (!label.empty()) {
    label += ':';
  }
  label += PercentEncode(secret.account);
  std::string uri =
      std::string(kScheme) + label + "?secret=" + Base32Encode(secret.key);
  if (!secret.issuer.empty()) {
    uri += "&issuer=" + PercentEncode(secret.issuer);
  }
  switch (secret.algorithm) {
    case HmacAlgorithm::kSha1:
      break;
    case HmacAlgorithm::kSha256:
      uri += "&algorithm=SHA256";
      break;
    case HmacAlgorithm::kSha512:
      uri += "&algorithm=SHA512";
      break;
  }
  if (secret.digits != 6) {
    uri += "&digits=" + std::to_string(secret.digits);
  }
  if (secret.period != 30) {
    uri += "&period=" + std::to_string(secret.period);
  }
  return uri;
}

std::string TotpCode(const TotpSecret& secret, std::int64_t unix_time) {
  const std::uint64_t counter =
      unix_time < 0 ? 0
                    : static_cast<std::uint64_t>(unix_time) /
                          static_cast<std::uint64_t>(secret.period);
  std::string message(8, '\0');
  for (int i = 7; i >= 0; --i) {
    message[static_cast<std::size_t>(i)] =
        static_cast<char>((counter >> ((7 - i) * 8)) & 0xFF);
  }
  std::string mac = Hmac(secret.algorithm, secret.key, message);
  // Dynamic truncation (RFC 4226, section 5.3).
  const std::size_t offset = static_cast<unsigned char>(mac.back()) & 0x0F;
  const auto byte = [&](std::size_t i) {
    return static_cast<std::uint32_t>(
        static_cast<unsigned char>(mac[offset + i]));
  };
  std::uint32_t value =
      ((byte(0) & 0x7F) << 24) | (byte(1) << 16) | (byte(2) << 8) | byte(3);
  std::uint32_t modulus = 1;
  for (int i = 0; i < secret.digits; ++i) {
    modulus *= 10;
  }
  std::string code = std::to_string(value % modulus);
  code.insert(0, static_cast<std::size_t>(secret.digits) - code.size(), '0');
  Wipe(mac);
  return code;
}

int TotpSecondsLeft(const TotpSecret& secret, std::int64_t unix_time) {
  std::int64_t into = unix_time % secret.period;
  if (into < 0) {
    into += secret.period;
  }
  return secret.period - static_cast<int>(into);
}

}  // namespace brouk
