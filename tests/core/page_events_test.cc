#include "core/page_events.h"

#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(PageEventsTest, PopupBlockedWithHttpUrl) {
  std::optional<PageEvent> event =
      ParsePageEvent("popup-blocked", "https://example.com/x?y");
  ASSERT_TRUE(event);
  EXPECT_EQ(event->type, PageEvent::Type::kPopupBlocked);
  EXPECT_EQ(event->url, "https://example.com/x?y");
  EXPECT_TRUE(ParsePageEvent("popup-blocked", "http://a/"));
}

TEST(PageEventsTest, PopupBlockedRejectsOtherSchemesAndHugeUrls) {
  EXPECT_FALSE(ParsePageEvent("popup-blocked", "javascript:alert(1)"));
  EXPECT_FALSE(ParsePageEvent("popup-blocked", "file:///etc/passwd"));
  EXPECT_FALSE(ParsePageEvent("popup-blocked", ""));
  EXPECT_FALSE(
      ParsePageEvent("popup-blocked", "https://a/" + std::string(3000, 'x')));
}

TEST(PageEventsTest, AudioEvents) {
  EXPECT_EQ(ParsePageEvent("audible", "")->type, PageEvent::Type::kAudible);
  EXPECT_EQ(ParsePageEvent("inaudible", "")->type, PageEvent::Type::kInaudible);
  EXPECT_FALSE(ParsePageEvent("audible", "unexpected payload"));
}

TEST(PageEventsTest, DisplayMediaRequest) {
  std::optional<PageEvent> event = ParsePageEvent("display-media", "7");
  ASSERT_TRUE(event);
  EXPECT_EQ(event->type, PageEvent::Type::kDisplayMedia);
  EXPECT_EQ(event->request_id, 7u);
  EXPECT_FALSE(event->wants_audio);

  event = ParsePageEvent("display-media", "4294967295:audio");
  ASSERT_TRUE(event);
  EXPECT_EQ(event->request_id, 4294967295u);
  EXPECT_TRUE(event->wants_audio);
}

TEST(PageEventsTest, DisplayMediaRejectsBadIds) {
  EXPECT_FALSE(ParsePageEvent("display-media", ""));
  EXPECT_FALSE(ParsePageEvent("display-media", ":audio"));
  EXPECT_FALSE(ParsePageEvent("display-media", "-1"));
  EXPECT_FALSE(ParsePageEvent("display-media", "4294967296"));
  EXPECT_FALSE(ParsePageEvent("display-media", "12345678901"));
  EXPECT_FALSE(ParsePageEvent("display-media", "1 "));
  EXPECT_FALSE(ParsePageEvent("display-media", "0x10"));
  EXPECT_FALSE(ParsePageEvent("display-media", "1:video"));
}

TEST(PageEventsTest, UnknownTypes) {
  EXPECT_FALSE(ParsePageEvent("open-file", "/etc/passwd"));
  EXPECT_FALSE(ParsePageEvent("", ""));
}

}  // namespace
}  // namespace brouk
