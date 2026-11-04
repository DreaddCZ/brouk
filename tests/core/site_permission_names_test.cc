#include "core/site_permission_names.h"

#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(SitePermissionNamesTest, NamesGoBothWays) {
  EXPECT_EQ(ContentSettingName(PermissionKind::kCamera), "media_stream_camera");
  EXPECT_EQ(ContentSettingName(PermissionKind::kWindowManagement),
            "window_placement");
  EXPECT_EQ(ContentSettingName(PermissionKind::kScreenShare), "");
  EXPECT_EQ(PermissionKindFromContentSettingName("geolocation"),
            PermissionKind::kLocation);
  EXPECT_FALSE(PermissionKindFromContentSettingName("cookies"));
  EXPECT_FALSE(PermissionKindFromContentSettingName(""));
}

TEST(SitePermissionNamesTest, OriginOfAPromptAnswer) {
  EXPECT_EQ(OriginFromSitePattern("https://example.com:443,*"),
            "https://example.com");
  EXPECT_EQ(OriginFromSitePattern("http://example.com:80,*"),
            "http://example.com");
  EXPECT_EQ(OriginFromSitePattern("http://localhost:8765,*"),
            "http://localhost:8765");
  EXPECT_EQ(OriginFromSitePattern("https://a.example.com:443,"
                                  "https://a.example.com:443"),
            "https://a.example.com");
  EXPECT_EQ(OriginFromSitePattern("https://[::1]:8443,*"),
            "https://[::1]:8443");
  EXPECT_EQ(OriginFromSitePattern("https://[::1]:443,*"), "https://[::1]");
}

TEST(SitePermissionNamesTest, OtherPatternsGiveNothing) {
  EXPECT_FALSE(OriginFromSitePattern("[*.]example.com,*"));
  EXPECT_FALSE(OriginFromSitePattern("https://[*.]example.com:443,*"));
  EXPECT_FALSE(OriginFromSitePattern("*,*"));
  EXPECT_FALSE(OriginFromSitePattern("file:///home/,*"));
  EXPECT_FALSE(OriginFromSitePattern("https://example.com:443,https://a.com"));
  EXPECT_FALSE(OriginFromSitePattern("https://example.com:443/path,*"));
  EXPECT_FALSE(OriginFromSitePattern("https://example.com:x,*"));
  EXPECT_FALSE(OriginFromSitePattern("https://:443,*"));
  EXPECT_FALSE(OriginFromSitePattern(""));
}

}  // namespace
}  // namespace brouk
