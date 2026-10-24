#include "core/crypto.h"

#include <sodium.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace brouk {

namespace {

// libsodium needs one initialisation per process (thread-safe, idempotent).
void EnsureSodium() {
  static const bool ready = sodium_init() >= 0;
  if (!ready) {
    // No secure random source: nothing here can work safely.
    std::abort();
  }
}

const unsigned char* Bytes(std::string_view text) {
  return reinterpret_cast<const unsigned char*>(text.data());
}

unsigned char* MutableBytes(std::string& text) {
  return reinterpret_cast<unsigned char*>(text.data());
}

std::uint32_t RotateLeft(std::uint32_t value, int bits) {
  return (value << bits) | (value >> (32 - bits));
}

// SHA-1 (FIPS 180-4). Only for the breach check's public hash prefix and
// TOTP (RFC 6238), which are defined over it; never for protecting data.
std::array<std::uint8_t, 20> Sha1Digest(std::string_view data) {
  std::array<std::uint32_t, 5> h = {0x67452301, 0xEFCDAB89, 0x98BADCFE,
                                    0x10325476, 0xC3D2E1F0};
  std::string message(data);
  const std::uint64_t bit_length = std::uint64_t{data.size()} * 8;
  message.push_back(static_cast<char>(0x80));
  while (message.size() % 64 != 56) {
    message.push_back('\0');
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    message.push_back(static_cast<char>((bit_length >> shift) & 0xFF));
  }
  std::array<std::uint32_t, 80> w{};
  for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
    for (std::size_t i = 0; i < 16; ++i) {
      const auto byte = [&](std::size_t k) {
        return static_cast<std::uint32_t>(
            static_cast<unsigned char>(message[chunk + (i * 4) + k]));
      };
      w[i] = (byte(0) << 24) | (byte(1) << 16) | (byte(2) << 8) | byte(3);
    }
    for (std::size_t i = 16; i < 80; ++i) {
      w[i] = RotateLeft(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    std::uint32_t a = h[0];
    std::uint32_t b = h[1];
    std::uint32_t c = h[2];
    std::uint32_t d = h[3];
    std::uint32_t e = h[4];
    for (std::size_t i = 0; i < 80; ++i) {
      std::uint32_t f = 0;
      std::uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      const std::uint32_t temp = RotateLeft(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = RotateLeft(b, 30);
      b = a;
      a = temp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  sodium_memzero(message.data(), message.size());  // May hold an HMAC key.
  std::array<std::uint8_t, 20> digest{};
  for (std::size_t i = 0; i < 5; ++i) {
    digest[i * 4] = static_cast<std::uint8_t>(h[i] >> 24);
    digest[(i * 4) + 1] = static_cast<std::uint8_t>(h[i] >> 16);
    digest[(i * 4) + 2] = static_cast<std::uint8_t>(h[i] >> 8);
    digest[(i * 4) + 3] = static_cast<std::uint8_t>(h[i]);
  }
  return digest;
}

std::string HmacSha1(std::string_view key, std::string_view message) {
  constexpr std::size_t kBlock = 64;
  std::string block_key(key.size() > kBlock ? Sha1(key) : std::string(key));
  block_key.resize(kBlock, '\0');
  std::string inner(kBlock, '\0');
  std::string outer(kBlock, '\0');
  for (std::size_t i = 0; i < kBlock; ++i) {
    inner[i] = static_cast<char>(block_key[i] ^ 0x36);
    outer[i] = static_cast<char>(block_key[i] ^ 0x5c);
  }
  inner.append(message);
  outer.append(Sha1(inner));
  std::string result = Sha1(outer);
  Wipe(block_key);
  Wipe(inner);
  Wipe(outer);
  return result;
}

}  // namespace

SecretKey SecretKey::Generate() {
  EnsureSodium();
  SecretKey key;
  crypto_aead_xchacha20poly1305_ietf_keygen(key.bytes_.data());
  return key;
}

std::optional<SecretKey> SecretKey::FromBytes(std::string_view bytes) {
  if (bytes.size() != kSize) {
    return std::nullopt;
  }
  SecretKey key;
  std::memcpy(key.bytes_.data(), bytes.data(), kSize);
  return key;
}

SecretKey::~SecretKey() {
  sodium_memzero(bytes_.data(), bytes_.size());
}

std::string_view SecretKey::bytes() const {
  return {reinterpret_cast<const char*>(bytes_.data()), bytes_.size()};
}

std::string RandomBytes(std::size_t count) {
  EnsureSodium();
  std::string bytes(count, '\0');
  randombytes_buf(bytes.data(), bytes.size());
  return bytes;
}

std::uint32_t RandomUniform(std::uint32_t upper) {
  EnsureSodium();
  return randombytes_uniform(upper);
}

std::string Encrypt(const SecretKey& key,
                    std::string_view plaintext,
                    std::string_view associated) {
  EnsureSodium();
  constexpr std::size_t kNonce = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
  constexpr std::size_t kTag = crypto_aead_xchacha20poly1305_ietf_ABYTES;
  std::string out = RandomBytes(kNonce);
  out.resize(kNonce + plaintext.size() + kTag);
  unsigned long long written = 0;
  crypto_aead_xchacha20poly1305_ietf_encrypt(
      MutableBytes(out) + kNonce, &written, Bytes(plaintext), plaintext.size(),
      Bytes(associated), associated.size(), nullptr, Bytes(out),
      Bytes(key.bytes()));
  out.resize(kNonce + written);
  return out;
}

std::optional<std::string> Decrypt(const SecretKey& key,
                                   std::string_view data,
                                   std::string_view associated) {
  EnsureSodium();
  constexpr std::size_t kNonce = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
  constexpr std::size_t kTag = crypto_aead_xchacha20poly1305_ietf_ABYTES;
  if (data.size() < kNonce + kTag) {
    return std::nullopt;
  }
  std::string plain(data.size() - kNonce - kTag, '\0');
  unsigned long long written = 0;
  if (crypto_aead_xchacha20poly1305_ietf_decrypt(
          MutableBytes(plain), &written, nullptr, Bytes(data) + kNonce,
          data.size() - kNonce, Bytes(associated), associated.size(),
          Bytes(data), Bytes(key.bytes())) != 0) {
    return std::nullopt;
  }
  plain.resize(written);
  return plain;
}

std::optional<SecretKey> DeriveKeyFromPassword(std::string_view password,
                                               std::string_view salt,
                                               PasswordKdfCost cost) {
  EnsureSodium();
  if (salt.size() != crypto_pwhash_SALTBYTES ||
      cost.ops < crypto_pwhash_OPSLIMIT_MIN ||
      cost.memory < crypto_pwhash_MEMLIMIT_MIN) {
    return std::nullopt;
  }
  std::string key(SecretKey::kSize, '\0');
  if (crypto_pwhash(MutableBytes(key), key.size(), password.data(),
                    password.size(), Bytes(salt), cost.ops, cost.memory,
                    crypto_pwhash_ALG_ARGON2ID13) != 0) {
    return std::nullopt;  // Out of memory.
  }
  std::optional<SecretKey> result = SecretKey::FromBytes(key);
  Wipe(key);
  return result;
}

static_assert(kPasswordSaltSize == crypto_pwhash_SALTBYTES);

std::string Sha1(std::string_view data) {
  std::array<std::uint8_t, 20> digest = Sha1Digest(data);
  return {reinterpret_cast<const char*>(digest.data()), digest.size()};
}

std::string Hmac(HmacAlgorithm algorithm,
                 std::string_view key,
                 std::string_view message) {
  EnsureSodium();
  switch (algorithm) {
    case HmacAlgorithm::kSha1:
      return HmacSha1(key, message);
    case HmacAlgorithm::kSha256: {
      crypto_auth_hmacsha256_state state;
      std::string out(crypto_auth_hmacsha256_BYTES, '\0');
      crypto_auth_hmacsha256_init(&state, Bytes(key), key.size());
      crypto_auth_hmacsha256_update(&state, Bytes(message), message.size());
      crypto_auth_hmacsha256_final(&state, MutableBytes(out));
      sodium_memzero(&state, sizeof(state));
      return out;
    }
    case HmacAlgorithm::kSha512: {
      crypto_auth_hmacsha512_state state;
      std::string out(crypto_auth_hmacsha512_BYTES, '\0');
      crypto_auth_hmacsha512_init(&state, Bytes(key), key.size());
      crypto_auth_hmacsha512_update(&state, Bytes(message), message.size());
      crypto_auth_hmacsha512_final(&state, MutableBytes(out));
      sodium_memzero(&state, sizeof(state));
      return out;
    }
  }
  return {};
}

std::string HexEncode(std::string_view bytes) {
  constexpr std::string_view kDigits = "0123456789ABCDEF";
  std::string hex;
  hex.reserve(bytes.size() * 2);
  for (char c : bytes) {
    const auto byte = static_cast<unsigned char>(c);
    hex.push_back(kDigits[byte >> 4]);
    hex.push_back(kDigits[byte & 0x0F]);
  }
  return hex;
}

std::string Base64Encode(std::string_view bytes) {
  EnsureSodium();
  constexpr int kVariant = sodium_base64_VARIANT_ORIGINAL;
  std::string text(sodium_base64_ENCODED_LEN(bytes.size(), kVariant), '\0');
  sodium_bin2base64(text.data(), text.size(), Bytes(bytes), bytes.size(),
                    kVariant);
  text.resize(text.size() - 1);  // The terminating NUL.
  return text;
}

std::optional<std::string> Base64Decode(std::string_view text) {
  EnsureSodium();
  std::string bytes((text.size() / 4 * 3) + 3, '\0');
  std::size_t written = 0;
  const char* end = nullptr;
  if (sodium_base642bin(MutableBytes(bytes), bytes.size(), text.data(),
                        text.size(), "\n\r ", &written, &end,
                        sodium_base64_VARIANT_ORIGINAL) != 0 ||
      end != text.data() + text.size()) {
    return std::nullopt;
  }
  bytes.resize(written);
  return bytes;
}

std::optional<std::string> Base32Decode(std::string_view text) {
  std::string bytes;
  std::uint32_t buffer = 0;
  int bits = 0;
  for (char c : text) {
    int value = -1;
    if (c >= 'A' && c <= 'Z') {
      value = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
      value = c - 'a';
    } else if (c >= '2' && c <= '7') {
      value = c - '2' + 26;
    } else if (c == ' ' || c == '-' || c == '=') {
      continue;
    } else {
      return std::nullopt;
    }
    buffer = (buffer << 5) | static_cast<std::uint32_t>(value);
    bits += 5;
    if (bits >= 8) {
      bits -= 8;
      bytes.push_back(static_cast<char>((buffer >> bits) & 0xFF));
    }
  }
  return bytes;
}

void Wipe(std::string& secret) {
  sodium_memzero(secret.data(), secret.size());
  secret.clear();
}

bool SecretsEqual(std::string_view a, std::string_view b) {
  EnsureSodium();
  return a.size() == b.size() &&
         sodium_memcmp(a.data(), b.data(), a.size()) == 0;
}

}  // namespace brouk
