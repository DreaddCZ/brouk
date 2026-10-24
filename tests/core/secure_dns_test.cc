#include "core/secure_dns.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(SecureDnsTest, Providers) {
  EXPECT_EQ(SecureDnsTemplate("off", "https://x.example/"), std::nullopt);
  EXPECT_EQ(SecureDnsTemplate("cloudflare", ""),
            "https://cloudflare-dns.com/dns-query");
  EXPECT_EQ(SecureDnsTemplate("adguard", ""),
            "https://dns.adguard-dns.com/dns-query");
  EXPECT_EQ(SecureDnsTemplate("unknown", ""), std::nullopt);
}

TEST(SecureDnsTest, CustomNeedsAnHttpsAddress) {
  EXPECT_EQ(SecureDnsTemplate("custom", " https://dns.nextdns.io/abc123 "),
            "https://dns.nextdns.io/abc123");
  EXPECT_EQ(SecureDnsTemplate("custom", "http://dns.example/"), std::nullopt);
  EXPECT_EQ(SecureDnsTemplate("custom", "https://"), std::nullopt);
  EXPECT_EQ(SecureDnsTemplate("custom", "https://a b/"), std::nullopt);
  EXPECT_EQ(SecureDnsTemplate("custom", ""), std::nullopt);
}

}  // namespace
}  // namespace brouk
