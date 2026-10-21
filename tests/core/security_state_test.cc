#include "core/security_state.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

SecurityLevel Level(const char* url,
                    bool secure = false,
                    bool cert_error = false,
                    bool insecure_content = false) {
  return SecurityLevelFor({url, secure, cert_error, insecure_content});
}

TEST(SecurityStateTest, ValidHttpsIsSecure) {
  EXPECT_EQ(Level("https://example.com/", true), SecurityLevel::kSecure);
}

TEST(SecurityStateTest, MixedContentIsNotSecure) {
  EXPECT_EQ(Level("https://example.com/", true, false, true),
            SecurityLevel::kNotSecure);
}

TEST(SecurityStateTest, CertificateErrorIsDangerous) {
  EXPECT_EQ(Level("https://expired.badssl.com/", false, true),
            SecurityLevel::kDangerous);
}

TEST(SecurityStateTest, HttpIsNotSecureExceptLocalhost) {
  EXPECT_EQ(Level("http://example.com/"), SecurityLevel::kNotSecure);
  EXPECT_EQ(Level("HTTP://Example.com/"), SecurityLevel::kNotSecure);
  EXPECT_EQ(Level("http://localhost:8765/x"), SecurityLevel::kNone);
  EXPECT_EQ(Level("http://127.0.0.1/"), SecurityLevel::kNone);
  EXPECT_EQ(Level("http://app.localhost/"), SecurityLevel::kNone);
  EXPECT_EQ(Level("http://localhost.evil.com/"), SecurityLevel::kNotSecure);
}

TEST(SecurityStateTest, LocalPagesHaveNoState) {
  EXPECT_EQ(Level("file:///Users/me/a.html"), SecurityLevel::kNone);
  EXPECT_EQ(Level("about:blank"), SecurityLevel::kNone);
  EXPECT_EQ(Level(""), SecurityLevel::kNone);
}

}  // namespace
}  // namespace brouk
