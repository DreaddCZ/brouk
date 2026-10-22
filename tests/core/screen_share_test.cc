#include "core/screen_share.h"

#include <chrono>

#include <gtest/gtest.h>

namespace brouk {
namespace {

using std::chrono::seconds;

TEST(ScreenShareTest, DesktopSourceIds) {
  EXPECT_EQ(DesktopSourceId({DesktopSource::Type::kScreen, 1}), "screen:1:0");
  EXPECT_EQ(DesktopSourceId({DesktopSource::Type::kWindow, 4294967295u}),
            "window:4294967295:0");
}

TEST(ScreenShareTest, ValidSourceIds) {
  EXPECT_TRUE(IsValidDesktopSourceId("screen:1:0"));
  EXPECT_TRUE(IsValidDesktopSourceId("window:0:0"));
  EXPECT_TRUE(IsValidDesktopSourceId("window:4294967295:0"));
}

TEST(ScreenShareTest, InvalidSourceIds) {
  EXPECT_FALSE(IsValidDesktopSourceId(""));
  EXPECT_FALSE(IsValidDesktopSourceId("screen:"));
  EXPECT_FALSE(IsValidDesktopSourceId("screen::0"));
  EXPECT_FALSE(IsValidDesktopSourceId("screen:1"));
  EXPECT_FALSE(IsValidDesktopSourceId("screen:1:1"));
  EXPECT_FALSE(IsValidDesktopSourceId("screen:-1:0"));
  EXPECT_FALSE(IsValidDesktopSourceId("screen:01:0"));
  EXPECT_FALSE(IsValidDesktopSourceId("window:4294967296:0"));
  EXPECT_FALSE(IsValidDesktopSourceId("tab:1:0"));
  EXPECT_FALSE(IsValidDesktopSourceId("screen:1:0'); alert(1); ('"));
}

TEST(ScreenShareTest, GrantIsUsedOnceBySameOrigin) {
  ScreenShareGrant grant;
  ScreenShareGrant::Clock::time_point t0;
  EXPECT_FALSE(grant.Consume("https://a.example", t0));
  grant.Issue("https://a.example", t0);
  EXPECT_FALSE(grant.Consume("https://b.example", t0 + seconds(1)));
  EXPECT_TRUE(grant.Consume("https://a.example", t0 + seconds(1)));
  EXPECT_FALSE(grant.Consume("https://a.example", t0 + seconds(2)));
}

TEST(ScreenShareTest, GrantExpires) {
  ScreenShareGrant grant;
  ScreenShareGrant::Clock::time_point t0;
  grant.Issue("https://a.example", t0);
  EXPECT_FALSE(grant.Consume("https://a.example", t0 + seconds(11)));
  grant.Issue("https://a.example", t0);
  EXPECT_TRUE(grant.Consume("https://a.example", t0 + seconds(10)));
}

TEST(ScreenShareTest, GrantNeedsAnOrigin) {
  ScreenShareGrant grant;
  ScreenShareGrant::Clock::time_point t0;
  grant.Issue("", t0);
  EXPECT_FALSE(grant.Consume("", t0));
}

}  // namespace
}  // namespace brouk
