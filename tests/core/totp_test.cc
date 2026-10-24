#include "core/totp.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

// RFC 6238, appendix B: the seeds are ASCII digits.
TotpSecret RfcSecret(HmacAlgorithm algorithm, std::string key) {
  TotpSecret secret;
  secret.key = std::move(key);
  secret.algorithm = algorithm;
  secret.digits = 8;
  return secret;
}

TEST(TotpTest, MatchesRfc6238Vectors) {
  TotpSecret sha1 = RfcSecret(HmacAlgorithm::kSha1, "12345678901234567890");
  TotpSecret sha256 =
      RfcSecret(HmacAlgorithm::kSha256, "12345678901234567890123456789012");
  TotpSecret sha512 = RfcSecret(
      HmacAlgorithm::kSha512,
      "1234567890123456789012345678901234567890123456789012345678901234");
  EXPECT_EQ(TotpCode(sha1, 59), "94287082");
  EXPECT_EQ(TotpCode(sha256, 59), "46119246");
  EXPECT_EQ(TotpCode(sha512, 59), "90693936");
  EXPECT_EQ(TotpCode(sha1, 1111111109), "07081804");
  EXPECT_EQ(TotpCode(sha256, 1111111111), "67062674");
  EXPECT_EQ(TotpCode(sha512, 1234567890), "93441116");
  EXPECT_EQ(TotpCode(sha1, 20000000000), "65353130");
}

TEST(TotpTest, SixDigitCodesArePadded) {
  TotpSecret secret = RfcSecret(HmacAlgorithm::kSha1, "12345678901234567890");
  secret.digits = 6;
  EXPECT_EQ(TotpCode(secret, 1111111109), "081804");
}

TEST(TotpTest, SecondsLeft) {
  TotpSecret secret;
  EXPECT_EQ(TotpSecondsLeft(secret, 60), 30);
  EXPECT_EQ(TotpSecondsLeft(secret, 89), 1);
}

TEST(TotpTest, ParsesBase32Secret) {
  std::optional<TotpSecret> secret = ParseTotpSecret("  jbsw y3dp ehpk 3pxp ");
  ASSERT_TRUE(secret);
  EXPECT_EQ(secret->key, "Hello!\xde\xad\xbe\xef");
  EXPECT_EQ(secret->digits, 6);
  EXPECT_EQ(secret->period, 30);
  // Under 80 bits, or not base32.
  EXPECT_FALSE(ParseTotpSecret("JBSWY3DP"));
  EXPECT_FALSE(ParseTotpSecret("not a secret!"));
  EXPECT_FALSE(ParseTotpSecret(""));
}

TEST(TotpTest, ParsesKeyUri) {
  std::optional<TotpSecret> secret = ParseTotpSecret(
      "otpauth://totp/ACME%20Co:john.doe@email.com?"
      "secret=HXDMVJECJJWSRB3HWIZR4IFUGFTMXBOZ&issuer=ACME%20Co&"
      "algorithm=SHA256&digits=8&period=60");
  ASSERT_TRUE(secret);
  EXPECT_EQ(secret->issuer, "ACME Co");
  EXPECT_EQ(secret->account, "john.doe@email.com");
  EXPECT_EQ(secret->algorithm, HmacAlgorithm::kSha256);
  EXPECT_EQ(secret->digits, 8);
  EXPECT_EQ(secret->period, 60);
  // The issuer from the label when there is no parameter.
  std::optional<TotpSecret> label_only =
      ParseTotpSecret("otpauth://totp/GitHub:me?secret=JBSWY3DPEHPK3PXP");
  ASSERT_TRUE(label_only);
  EXPECT_EQ(label_only->issuer, "GitHub");
  EXPECT_EQ(label_only->account, "me");
}

TEST(TotpTest, RejectsInvalidUris) {
  EXPECT_FALSE(ParseTotpSecret("otpauth://totp/x?issuer=a"));  // No secret.
  EXPECT_FALSE(ParseTotpSecret("otpauth://hotp/x?secret=JBSWY3DPEHPK3PXP"));
  EXPECT_FALSE(
      ParseTotpSecret("otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&digits=4"));
  EXPECT_FALSE(
      ParseTotpSecret("otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&period=0"));
  EXPECT_FALSE(ParseTotpSecret(
      "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP&algorithm=MD5"));
}

TEST(TotpTest, UriRoundTrips) {
  std::optional<TotpSecret> secret = ParseTotpSecret(
      "otpauth://totp/Big%20Corp:me%40corp.com?secret=JBSWY3DPEHPK3PXP&"
      "digits=7&algorithm=SHA512");
  ASSERT_TRUE(secret);
  std::string uri = TotpSecretToUri(*secret);
  std::optional<TotpSecret> again = ParseTotpSecret(uri);
  ASSERT_TRUE(again);
  EXPECT_EQ(again->key, secret->key);
  EXPECT_EQ(again->issuer, "Big Corp");
  EXPECT_EQ(again->account, "me@corp.com");
  EXPECT_EQ(again->digits, 7);
  EXPECT_EQ(again->algorithm, HmacAlgorithm::kSha512);
  EXPECT_EQ(TotpCode(*again, 1000), TotpCode(*secret, 1000));
}

}  // namespace
}  // namespace brouk
