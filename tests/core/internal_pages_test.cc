#include "core/internal_pages.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(InternalPagesTest, KnownPages) {
  EXPECT_TRUE(IsInternalPageUrl("brouk://profiles/"));
  EXPECT_TRUE(IsInternalPageUrl("brouk://profiles"));
  EXPECT_FALSE(IsInternalPageUrl("brouk://unknown/"));
  EXPECT_FALSE(IsInternalPageUrl("https://profiles/"));
  EXPECT_FALSE(IsInternalPageUrl("brouk:profiles"));
  EXPECT_EQ(InternalPageOf("brouk://profiles/app.js"), "profiles");
  EXPECT_FALSE(InternalPageOf("brouk://evil/"));
  EXPECT_EQ(InternalPageOf("brouk://task-manager/"), "task-manager");
  EXPECT_EQ(InternalResourcePath("brouk://task-manager/"),
            "task-manager/index.html");
}

TEST(InternalPagesTest, ResourcePaths) {
  EXPECT_EQ(InternalResourcePath("brouk://profiles/"), "profiles/index.html");
  EXPECT_EQ(InternalResourcePath("brouk://profiles"), "profiles/index.html");
  EXPECT_EQ(InternalResourcePath("brouk://profiles/?x=1#y"),
            "profiles/index.html");
  EXPECT_EQ(InternalResourcePath("brouk://profiles/profiles.js"),
            "profiles/profiles.js");
  EXPECT_EQ(InternalResourcePath("brouk://profiles/shared/bridge.js"),
            "shared/bridge.js");
  EXPECT_EQ(InternalResourcePath("brouk://profiles/style.css?v=2"),
            "profiles/style.css");
}

TEST(InternalPagesTest, RefusesEverythingElse) {
  EXPECT_FALSE(InternalResourcePath("brouk://profiles/../settings.yaml"));
  EXPECT_FALSE(InternalResourcePath("brouk://profiles/..%2Fx.js"));
  EXPECT_FALSE(InternalResourcePath("brouk://profiles/a/b.js"));
  EXPECT_FALSE(InternalResourcePath("brouk://profiles/shared/../x.js"));
  EXPECT_FALSE(InternalResourcePath("brouk://profiles/.hidden.js"));
  EXPECT_FALSE(InternalResourcePath("brouk://profiles/app.ts"));
  EXPECT_FALSE(InternalResourcePath("brouk://profiles/app.test"));
  EXPECT_FALSE(InternalResourcePath("brouk://profiles/.js"));
  EXPECT_FALSE(InternalResourcePath("brouk://profiles/x.js/"));
  EXPECT_FALSE(InternalResourcePath("brouk://other/index.html"));
}

TEST(InternalPagesTest, MimeTypes) {
  EXPECT_EQ(InternalResourceMimeType("a/index.html"), "text/html");
  EXPECT_EQ(InternalResourceMimeType("a/x.js"), "text/javascript");
  EXPECT_EQ(InternalResourceMimeType("a/x.css"), "text/css");
  EXPECT_EQ(InternalResourceMimeType("a/x.svg"), "image/svg+xml");
}

TEST(InternalPagesTest, CreditsAndItsNoticePages) {
  // 023: the licences page, and further notice pages next to it.
  EXPECT_EQ(InternalPageOf("brouk://credits/"), "credits");
  EXPECT_EQ(InternalResourcePath("brouk://credits/"), "credits/index.html");
  EXPECT_EQ(InternalResourcePath("brouk://credits/chromium"),
            "credits/chromium.html");
  EXPECT_EQ(InternalResourcePath("brouk://credits/rust-std#x"),
            "credits/rust-std.html");
  EXPECT_EQ(InternalResourcePath("brouk://credits/data.js"), "credits/data.js");
  EXPECT_FALSE(InternalResourcePath("brouk://credits/a/b"));
  EXPECT_FALSE(InternalResourcePath("brouk://credits/.hidden"));
  EXPECT_FALSE(InternalResourcePath("brouk://credits/..%2Fx"));
}

TEST(InternalPagesTest, HistoryPageExists) {
  EXPECT_TRUE(IsInternalPageUrl("brouk://history/"));
  EXPECT_EQ(InternalPageOf("brouk://bookmarks/#folder=5"), "bookmarks");
  EXPECT_EQ(InternalPageOf("brouk://history/"), "history");
  EXPECT_EQ(InternalResourcePath("brouk://history/"), "history/index.html");
}

}  // namespace
}  // namespace brouk
