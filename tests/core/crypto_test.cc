#include "core/crypto.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace brouk {
namespace {

TEST(CryptoTest, EncryptsAndDecrypts) {
  SecretKey key = SecretKey::Generate();
  std::string sealed = Encrypt(key, "hunter2", "password");
  EXPECT_EQ(sealed.find("hunter2"), std::string::npos);
  EXPECT_EQ(Decrypt(key, sealed, "password"), "hunter2");
  // A fresh nonce every time.
  EXPECT_NE(Encrypt(key, "hunter2", "password"), sealed);
  EXPECT_EQ(Decrypt(key, Encrypt(key, ""), ""), "");
}

TEST(CryptoTest, RejectsWrongKeyFieldOrChangedData) {
  SecretKey key = SecretKey::Generate();
  std::string sealed = Encrypt(key, "secret", "password");
  EXPECT_FALSE(Decrypt(SecretKey::Generate(), sealed, "password"));
  EXPECT_FALSE(Decrypt(key, sealed, "note"));
  std::string changed = sealed;
  changed.back() = static_cast<char>(changed.back() ^ 1);
  EXPECT_FALSE(Decrypt(key, changed, "password"));
  EXPECT_FALSE(Decrypt(key, "short", "password"));
}

TEST(CryptoTest, KeyRoundTripsThroughBytes) {
  SecretKey key = SecretKey::Generate();
  std::optional<SecretKey> copy = SecretKey::FromBytes(key.bytes());
  ASSERT_TRUE(copy);
  EXPECT_EQ(Decrypt(*copy, Encrypt(key, "x")), "x");
  EXPECT_FALSE(SecretKey::FromBytes("too short"));
}

TEST(CryptoTest, DerivesTheSameKeyFromTheSamePassword) {
  // The minimum cost keeps the test fast.
  PasswordKdfCost cost{1, std::size_t{8} * 1024};
  std::string salt(kPasswordSaltSize, 'a');
  std::optional<SecretKey> one = DeriveKeyFromPassword("pw", salt, cost);
  std::optional<SecretKey> two = DeriveKeyFromPassword("pw", salt, cost);
  std::optional<SecretKey> other = DeriveKeyFromPassword("pw2", salt, cost);
  ASSERT_TRUE(one && two && other);
  EXPECT_EQ(one->bytes(), two->bytes());
  EXPECT_NE(one->bytes(), other->bytes());
  EXPECT_FALSE(DeriveKeyFromPassword("pw", "short salt", cost));
}

TEST(CryptoTest, Sha1MatchesKnownVectors) {
  EXPECT_EQ(HexEncode(Sha1("")), "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709");
  EXPECT_EQ(HexEncode(Sha1("abc")), "A9993E364706816ABA3E25717850C26C9CD0D89D");
  EXPECT_EQ(HexEncode(Sha1(
                "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
            "84983E441C3BD26EBAAE4AA1F95129E5E54670F1");
  EXPECT_EQ(HexEncode(Sha1(std::string(1000000, 'a'))),
            "34AA973CD4C4DAA4F61EEB2BDBAD27316534016F");
}

TEST(CryptoTest, HmacMatchesRfc2202AndRfc4231) {
  std::string key(20, '\x0b');
  EXPECT_EQ(HexEncode(Hmac(HmacAlgorithm::kSha1, key, "Hi There")),
            "B617318655057264E28BC0B6FB378C8EF146BE00");
  EXPECT_EQ(HexEncode(Hmac(HmacAlgorithm::kSha1, "Jefe",
                           "what do ya want for nothing?")),
            "EFFCDF6AE5EB2FA2D27416D5F184DF9C259A7C79");
  // A key longer than the block size is hashed first.
  EXPECT_EQ(HexEncode(Hmac(HmacAlgorithm::kSha1, std::string(80, '\xaa'),
                           "Test Using Larger Than Block-Size Key - Hash Key "
                           "First")),
            "AA4AE5E15272D00E95705637CE8A3B55ED402112");
  EXPECT_EQ(HexEncode(Hmac(HmacAlgorithm::kSha256, key, "Hi There")),
            "B0344C61D8DB38535CA8AFCEAF0BF12B881DC200C9833DA726E9376C2E32CFF7");
  EXPECT_EQ(
      HexEncode(Hmac(HmacAlgorithm::kSha512, key, "Hi There")).substr(0, 32),
      "87AA7CDEA5EF619D4FF0B4241A1D6CB0");
}

TEST(CryptoTest, Encodings) {
  EXPECT_EQ(Base64Encode("hello"), "aGVsbG8=");
  EXPECT_EQ(Base64Decode("aGVsbG8="), "hello");
  EXPECT_EQ(Base64Decode(""), "");
  EXPECT_FALSE(Base64Decode("not base64!"));
  std::string binary("\x00\xff\x10", 3);
  EXPECT_EQ(Base64Decode(Base64Encode(binary)), binary);
  // RFC 4648 test vectors, plus what authenticator apps accept.
  EXPECT_EQ(Base32Decode("MZXW6YTBOI======"), "foobar");
  EXPECT_EQ(Base32Decode("mzxw 6ytb oi"), "foobar");
  EXPECT_FALSE(Base32Decode("MZXW1"));
  EXPECT_EQ(HexEncode("\x01\xab"), "01AB");
}

TEST(CryptoTest, RandomValues) {
  EXPECT_EQ(RandomBytes(16).size(), 16U);
  EXPECT_NE(RandomBytes(16), RandomBytes(16));
  for (int i = 0; i < 100; ++i) {
    EXPECT_LT(RandomUniform(5), 5U);
  }
}

TEST(CryptoTest, WipeAndCompare) {
  std::string secret = "secret";
  Wipe(secret);
  EXPECT_TRUE(secret.empty());
  EXPECT_TRUE(SecretsEqual("abc", "abc"));
  EXPECT_FALSE(SecretsEqual("abc", "abd"));
  EXPECT_FALSE(SecretsEqual("abc", "abcd"));
}

// RFC 8032, section 7.1, tests 1 and 2.
std::string FromHex(std::string_view hex) {
  std::string bytes;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(static_cast<char>(
        std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
  }
  return bytes;
}

TEST(CryptoTest, VerifiesEd25519Signatures) {
  const std::string key1 = FromHex(
      "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  const std::string signature1 = FromHex(
      "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb882"
      "1590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
  EXPECT_TRUE(VerifyEd25519(key1, signature1, ""));
  const std::string key2 = FromHex(
      "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
  const std::string signature2 = FromHex(
      "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1"
      "e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
  EXPECT_TRUE(VerifyEd25519(key2, signature2, FromHex("72")));

  // Another message, key or signature, or wrong sizes.
  EXPECT_FALSE(VerifyEd25519(key2, signature2, FromHex("73")));
  EXPECT_FALSE(VerifyEd25519(key1, signature2, FromHex("72")));
  EXPECT_FALSE(VerifyEd25519(key1, signature1, "x"));
  EXPECT_FALSE(VerifyEd25519(key1.substr(1), signature1, ""));
  EXPECT_FALSE(VerifyEd25519(key1, signature1.substr(1), ""));
  EXPECT_FALSE(VerifyEd25519("", "", ""));
}

}  // namespace
}  // namespace brouk
