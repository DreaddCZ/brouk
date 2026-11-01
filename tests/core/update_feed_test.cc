#include "core/update_feed.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/crypto.h"

namespace brouk {
namespace {

std::string Entry(const std::string& version,
                  const std::string& channel,
                  const std::string& extra = "") {
  return R"({"version": ")" + version + R"(", "channel": ")" + channel +
         R"(", "url": "https://example.com/Brouk-)" + version +
         R"(-setup.exe", "size": 1000, "ed_signature": ")" +
         Base64Encode(std::string(64, 's')) + "\"" + extra + "}";
}

std::vector<UpdateRelease> Parse(const std::string& entries) {
  Result<std::vector<UpdateRelease>> feed =
      ParseUpdateFeed(R"({"releases": [)" + entries + "]}");
  EXPECT_TRUE(feed.ok());
  return feed.ok() ? feed.value() : std::vector<UpdateRelease>{};
}

TEST(UpdateFeedTest, ReadsReleases) {
  std::vector<UpdateRelease> releases = Parse(Entry(
      "0.3.0", "beta", R"(, "notes_url": "https://example.com/tag/v0.3.0")"));
  ASSERT_EQ(releases.size(), 1U);
  EXPECT_EQ(releases[0].version, "0.3.0");
  EXPECT_EQ(releases[0].channel, "beta");
  EXPECT_EQ(releases[0].url, "https://example.com/Brouk-0.3.0-setup.exe");
  EXPECT_EQ(releases[0].size, 1000);
  EXPECT_EQ(releases[0].signature, std::string(64, 's'));
  EXPECT_EQ(releases[0].notes_url, "https://example.com/tag/v0.3.0");
}

TEST(UpdateFeedTest, LeavesOutInvalidEntries) {
  const std::string signature = Base64Encode(std::string(64, 's'));
  std::vector<UpdateRelease> releases = Parse(
      Entry("0.3", "stable") + "," + Entry("0.3.0", "nightly") + "," +
      R"({"version": "0.3.0", "url": "http://example.com/a.exe", "size": 1,
          "ed_signature": ")" +
      signature + R"("},)" +
      R"({"version": "0.3.0", "url": "https://example.com/a.exe", "size": 0,
          "ed_signature": ")" +
      signature + R"("},)" +
      R"({"version": "0.3.0", "url": "https://example.com/a.exe", "size": 1,
          "ed_signature": "c2hvcnQ="},)" +
      Entry("0.3.0", "stable", R"x(, "notes_url": "javascript:alert(1)")x") +
      R"(, "text", 3,)" + Entry("0.3.1", "stable"));
  ASSERT_EQ(releases.size(), 1U);
  EXPECT_EQ(releases[0].version, "0.3.1");
}

TEST(UpdateFeedTest, ChannelDefaultsToStable) {
  std::vector<UpdateRelease> releases = Parse(
      R"({"version": "1.0.0", "url": "https://example.com/a.exe", "size": 1,
          "ed_signature": ")" +
      Base64Encode(std::string(64, 's')) + R"("})");
  ASSERT_EQ(releases.size(), 1U);
  EXPECT_EQ(releases[0].channel, "stable");
}

TEST(UpdateFeedTest, RejectsOtherDocuments) {
  EXPECT_FALSE(ParseUpdateFeed("").ok());
  EXPECT_FALSE(ParseUpdateFeed("[]").ok());
  EXPECT_FALSE(ParseUpdateFeed(R"({"releases": {}})").ok());
  EXPECT_FALSE(ParseUpdateFeed("<rss/>").ok());
  EXPECT_TRUE(ParseUpdateFeed(R"({"releases": []})").ok());
}

TEST(UpdateFeedTest, ParsesVersions) {
  EXPECT_EQ(ParseVersion("0.2.1"), (std::array<std::uint32_t, 3>{0, 2, 1}));
  EXPECT_EQ(ParseVersion("10.20.300"),
            (std::array<std::uint32_t, 3>{10, 20, 300}));
  EXPECT_FALSE(ParseVersion(""));
  EXPECT_FALSE(ParseVersion("1.2"));
  EXPECT_FALSE(ParseVersion("1.2.3.4"));
  EXPECT_FALSE(ParseVersion("1.2.3-beta"));
  EXPECT_FALSE(ParseVersion("v1.2.3"));
  EXPECT_FALSE(ParseVersion("1.+2.3"));
  EXPECT_FALSE(ParseVersion("1..3"));
  EXPECT_FALSE(ParseVersion("99999999999.0.0"));
}

TEST(UpdateFeedTest, ChoosesTheNewestReleaseOfTheChannel) {
  std::vector<UpdateRelease> releases =
      Parse(Entry("0.9.0", "stable") + "," + Entry("0.10.0", "stable") + "," +
            Entry("0.11.0", "beta") + "," + Entry("0.2.0", "stable"));
  // Compared as numbers.
  EXPECT_EQ(ChooseUpdate(releases, "0.9.0", "stable")
                .value_or(UpdateRelease{})
                .version,
            "0.10.0");
  EXPECT_EQ(
      ChooseUpdate(releases, "0.9.0", "beta").value_or(UpdateRelease{}).version,
      "0.11.0");
}

TEST(UpdateFeedTest, NeverChoosesTheSameOrAnOlderVersion) {
  std::vector<UpdateRelease> releases =
      Parse(Entry("0.10.0", "stable") + "," + Entry("0.11.0", "beta"));
  EXPECT_FALSE(ChooseUpdate(releases, "0.10.0", "stable"));
  // Back from beta to stable: stays on the beta until a newer stable.
  EXPECT_FALSE(ChooseUpdate(releases, "0.11.0", "stable"));
  EXPECT_FALSE(ChooseUpdate(releases, "0.11.0", "beta"));
  EXPECT_FALSE(ChooseUpdate(releases, "not a version", "stable"));
  EXPECT_FALSE(ChooseUpdate({}, "0.1.0", "beta"));
}

}  // namespace
}  // namespace brouk
