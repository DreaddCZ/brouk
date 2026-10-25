#ifndef BROUK_CORE_TOTP_H_
#define BROUK_CORE_TOTP_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/crypto.h"

namespace brouk {

// A TOTP generator (RFC 6238) for 2FA codes (008), as set up by a site with
// an otpauth:// URI (usually shown as a QR code) or a base32 secret.
struct TotpSecret {
  std::string key;  // Raw bytes, decoded from base32.
  HmacAlgorithm algorithm = HmacAlgorithm::kSha1;
  int digits = 6;   // 6 to 8.
  int period = 30;  // Seconds, 1 to 300.
  // From an otpauth:// URI, for display; may be empty.
  std::string issuer;
  std::string account;
};

// Reads what the user pasted: an otpauth://totp/ URI (Google Authenticator's
// key URI format) or a bare base32 secret. nullopt if it is neither, if the
// secret is too short (under 80 bits) or a parameter is out of range. HOTP
// (counter-based) URIs are not supported.
std::optional<TotpSecret> ParseTotpSecret(std::string_view text);

// The text stored for a secret: a normalised otpauth:// URI, which keeps
// every parameter. ParseTotpSecret reads it back.
std::string TotpSecretToUri(const TotpSecret& secret);

// The code at `unix_time`, zero-padded to `digits`.
std::string TotpCode(const TotpSecret& secret, std::int64_t unix_time);
// Seconds until the code at `unix_time` changes.
int TotpSecondsLeft(const TotpSecret& secret, std::int64_t unix_time);

}  // namespace brouk

#endif  // BROUK_CORE_TOTP_H_
