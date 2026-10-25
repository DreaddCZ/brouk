#ifndef BROUK_CORE_CRYPTO_H_
#define BROUK_CORE_CRYPTO_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace brouk {

// The few cryptographic operations the password manager (008) and autofill
// (009) need, on top of libsodium. Nothing else in Brouk touches libsodium,
// and nothing here is home-made cryptography except SHA-1, which only the
// breach check (a public hash prefix) and TOTP codes use.

// A 256-bit key, wiped from memory when destroyed.
class SecretKey {
 public:
  static constexpr std::size_t kSize = 32;

  // A new random key.
  static SecretKey Generate();
  // nullopt unless `bytes` has exactly kSize bytes.
  static std::optional<SecretKey> FromBytes(std::string_view bytes);

  SecretKey(const SecretKey& other) = default;
  SecretKey& operator=(const SecretKey& other) = default;
  ~SecretKey();

  // The raw key, for the keychain. Do not keep copies.
  std::string_view bytes() const;

 private:
  SecretKey() = default;
  std::array<unsigned char, kSize> bytes_{};
};

// Cryptographically secure random bytes, and a uniform number in
// [0, upper) (upper > 0).
std::string RandomBytes(std::size_t count);
std::uint32_t RandomUniform(std::uint32_t upper);

// Authenticated encryption (XChaCha20-Poly1305): a random nonce followed by
// the ciphertext and its tag. `associated` is bound to the ciphertext but not
// stored (e.g. which field a value belongs to), so a value copied to another
// place fails to decrypt.
std::string Encrypt(const SecretKey& key,
                    std::string_view plaintext,
                    std::string_view associated = {});
// nullopt if the data was not encrypted with this key and associated data,
// or was changed.
std::optional<std::string> Decrypt(const SecretKey& key,
                                   std::string_view data,
                                   std::string_view associated = {});

// A key from a password (Argon2id), for encrypted exports. `salt` has
// kPasswordSaltSize bytes. The default cost takes about half a second.
inline constexpr std::size_t kPasswordSaltSize = 16;
struct PasswordKdfCost {
  std::uint64_t ops = 3;
  std::size_t memory = std::size_t{64} * 1024 * 1024;
};
std::optional<SecretKey> DeriveKeyFromPassword(std::string_view password,
                                               std::string_view salt,
                                               PasswordKdfCost cost = {});

// Hashes and message authentication, as raw bytes.
std::string Sha1(std::string_view data);
enum class HmacAlgorithm { kSha1, kSha256, kSha512 };
std::string Hmac(HmacAlgorithm algorithm,
                 std::string_view key,
                 std::string_view message);

// Encodings. Base32 (RFC 4648) accepts lower case, spaces and missing
// padding, as authenticator apps do; nullopt for other characters.
std::string HexEncode(std::string_view bytes);  // Upper case.
std::string Base64Encode(std::string_view bytes);
std::optional<std::string> Base64Decode(std::string_view text);
std::optional<std::string> Base32Decode(std::string_view text);

// Overwrites a string holding a secret before it is released.
void Wipe(std::string& secret);

// Compares secrets in constant time.
bool SecretsEqual(std::string_view a, std::string_view b);

}  // namespace brouk

#endif  // BROUK_CORE_CRYPTO_H_
