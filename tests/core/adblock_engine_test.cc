#include "core/adblock_engine.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

constexpr char kResources[] = R"json([
  {"name": "noopjs", "aliases": ["noop.js"],
   "kind": {"mime": "application/javascript"},
   "content": "KGZ1bmN0aW9uKCl7fSkoKTs="},
  {"name": "set-constant.js", "aliases": ["set-constant"], "kind": "template",
   "content": "d2luZG93LmJyb3VrX3t7MX19ID0ge3syfX07"}
])json";

std::unique_ptr<AdblockEngine> Engine(std::vector<AdblockList> lists) {
  std::unique_ptr<AdblockEngine> engine = AdblockEngine::Build(lists);
  EXPECT_TRUE(engine);
  return engine;
}

TEST(AdblockEngineTest, BlocksAndTellsWhichRule) {
  auto engine = Engine({{.text = "! Title: Ads\n||ads.example^\n"},
                        {.text = "||tracker.example^$third-party\n"}});
  AdblockMatch ad = engine->Check("https://ads.example/banner.js",
                                  "https://news.example/", "script");
  EXPECT_TRUE(ad.block);
  ASSERT_TRUE(ad.filter);
  EXPECT_EQ(ad.filter->list, 0);
  EXPECT_EQ(ad.filter->line, 1);
  EXPECT_EQ(ad.filter->text, "||ads.example^");
  AdblockMatch tracker = engine->Check("https://tracker.example/p.gif",
                                       "https://news.example/", "image");
  EXPECT_TRUE(tracker.block);
  EXPECT_EQ(tracker.filter->list, 1);
  // First party: $third-party does not match.
  EXPECT_FALSE(engine
                   ->Check("https://tracker.example/p.gif",
                           "https://tracker.example/", "image")
                   .block);
  EXPECT_FALSE(engine
                   ->Check("https://news.example/app.js",
                           "https://news.example/", "script")
                   .block);
}

TEST(AdblockEngineTest, ExceptionsAndImportant) {
  auto engine = Engine({{.text = "||cdn.example^\n"
                                 "@@||cdn.example/lib.js\n"
                                 "||evil.example^$important\n"
                                 "@@||evil.example^\n"}});
  AdblockMatch allowed = engine->Check("https://cdn.example/lib.js",
                                       "https://site.example/", "script");
  EXPECT_FALSE(allowed.block);
  ASSERT_TRUE(allowed.exception);
  EXPECT_EQ(allowed.exception->text, "@@||cdn.example/lib.js");
  EXPECT_TRUE(engine
                  ->Check("https://cdn.example/ad.js", "https://site.example/",
                          "script")
                  .block);
  AdblockMatch important = engine->Check("https://evil.example/x.js",
                                         "https://site.example/", "script");
  EXPECT_TRUE(important.block);
  EXPECT_TRUE(important.important);
}

TEST(AdblockEngineTest, HostsListsBlockDomainsAndSubdomains) {
  auto engine = Engine({{.text = "# hosts\n0.0.0.0 ads.example\n"
                                 "127.0.0.1 localhost\nplain.example\n",
                         .hosts_format = true}});
  EXPECT_TRUE(
      engine->Check("https://ads.example/", "https://ads.example/", "document")
          .block);
  EXPECT_TRUE(engine
                  ->Check("https://x.ads.example/a.js", "https://site.example/",
                          "script")
                  .block);
  EXPECT_TRUE(
      engine->Check("https://plain.example/", "https://site.example/", "image")
          .block);
  EXPECT_FALSE(
      engine->Check("http://localhost/", "http://localhost/", "document")
          .block);
}

TEST(AdblockEngineTest, RedirectsAndRemovedParameters) {
  auto engine = Engine({{.text = "||ads.example/lib.js$script,redirect=noopjs\n"
                                 "$removeparam=utm_source\n"}});
  ASSERT_TRUE(engine->UseResources(kResources));
  AdblockMatch redirect = engine->Check("https://ads.example/lib.js",
                                        "https://site.example/", "script");
  EXPECT_TRUE(redirect.block);
  EXPECT_TRUE(redirect.redirect.starts_with("data:")) << redirect.redirect;
  AdblockMatch cleaned =
      engine->Check("https://site.example/page?utm_source=x&id=1",
                    "https://site.example/", "document");
  EXPECT_FALSE(cleaned.block);
  EXPECT_EQ(cleaned.rewritten_url, "https://site.example/page?id=1");
  EXPECT_FALSE(engine->UseResources("not json"));
}

TEST(AdblockEngineTest, CosmeticRulesAndScriptlets) {
  auto engine = Engine({{.text = "news.example##.ad-banner\n"
                                 "##.sponsored\n"
                                 "news.example##+js(set-constant, flag, 1)\n"
                                 "news.example##div:has-text(Advertisement)\n",
                         .trusted = true}});
  ASSERT_TRUE(engine->UseResources(kResources));
  std::string page = engine->CosmeticResourcesJson("https://news.example/a");
  EXPECT_NE(page.find(".ad-banner"), std::string::npos) << page;
  EXPECT_NE(page.find("brouk_flag"), std::string::npos) << page;
  EXPECT_NE(page.find("has-text"), std::string::npos) << page;
  std::string other = engine->CosmeticResourcesJson("https://other.example/");
  EXPECT_EQ(other.find(".ad-banner"), std::string::npos);
  std::string generic = engine->HiddenSelectorsJson(
      R"({"classes": ["sponsored", "content"], "ids": [], "exceptions": []})");
  EXPECT_EQ(generic, R"([".sponsored"])");
}

TEST(AdblockEngineTest, SerializedEngineMatchesTheSame) {
  auto engine = Engine({{.text = "||ads.example^\n@@||ads.example/ok.js\n"}});
  std::vector<std::uint8_t> bytes = engine->Serialize();
  ASSERT_FALSE(bytes.empty());
  std::unique_ptr<AdblockEngine> loaded = AdblockEngine::Deserialize(bytes);
  ASSERT_TRUE(loaded);
  EXPECT_TRUE(
      loaded->Check("https://ads.example/a.js", "https://s.example/", "script")
          .block);
  EXPECT_FALSE(
      loaded->Check("https://ads.example/ok.js", "https://s.example/", "script")
          .block);
  std::vector<std::uint8_t> garbage = {1, 2, 3, 4};
  EXPECT_FALSE(AdblockEngine::Deserialize(garbage));
}

TEST(AdblockEngineTest, MatchingIsFast) {
  std::string list;
  for (int i = 0; i < 20000; ++i) {
    list += "||ads" + std::to_string(i) + ".example^\n";
    list += "/banner" + std::to_string(i) + "/*$image\n";
  }
  auto engine = Engine({{.text = list}});
  constexpr int kRequests = 2000;
  auto start = std::chrono::steady_clock::now();
  int blocked = 0;
  for (int i = 0; i < kRequests; ++i) {
    blocked += engine->Check("https://cdn" + std::to_string(i) +
                                 ".example/banner5/x.png",
                             "https://site.example/", "image")
                       .block
                   ? 1
                   : 0;
  }
  auto per_request = (std::chrono::steady_clock::now() - start) / kRequests;
  EXPECT_EQ(blocked, kRequests);
  // 007: well under 1 ms (generous for sanitizer builds).
  EXPECT_LT(per_request, std::chrono::microseconds(500));
}

TEST(AdblockEngineTest, ChecksFilterLines) {
  EXPECT_EQ(CheckFilterLine("||ads.example^").kind,
            FilterLineCheck::Kind::kRule);
  EXPECT_EQ(CheckFilterLine("example.com##.ad").kind,
            FilterLineCheck::Kind::kRule);
  EXPECT_EQ(CheckFilterLine("! a comment").kind,
            FilterLineCheck::Kind::kCommentOrEmpty);
  EXPECT_EQ(CheckFilterLine("   ").kind,
            FilterLineCheck::Kind::kCommentOrEmpty);
  FilterLineCheck bad = CheckFilterLine("||ads.example^$nonsense-option");
  EXPECT_EQ(bad.kind, FilterLineCheck::Kind::kInvalid);
  EXPECT_FALSE(bad.error.empty());
}

}  // namespace
}  // namespace brouk
