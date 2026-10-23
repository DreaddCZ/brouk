#include "core/request_log.h"

#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

constexpr char kJwt[] =
    "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjMiLCJleHAiOjE5MDAwMDAwMDB9."
    "c2lnbmF0dXJl";

TEST(RequestLogTest, RecordsARequestAndItsResponse) {
  RequestLog log;
  log.OnRequest("1", "https://api.example/x", "POST", "Fetch",
                {{"Content-Type", "application/json"}},
                std::string("{\"a\":1}"), 1000.0, 5.0);
  log.OnRequestExtraHeaders(
      "1", {{"Cookie", "s=1"}, {"Content-Type", "application/json"}});
  log.OnResponse("1", 201, "application/json", {{"X-A", "b"}});
  log.OnFinished("1", 5.25, 42);
  ASSERT_EQ(log.entries().size(), 1u);
  const RecordedRequest& entry = log.entries().front();
  EXPECT_EQ(entry.seq, 1u);
  EXPECT_EQ(entry.status, 201);
  EXPECT_EQ(entry.request_headers.size(), 2u);
  EXPECT_EQ(entry.request_headers[0].first, "Cookie");
  EXPECT_EQ(*entry.post_data, "{\"a\":1}");
  EXPECT_DOUBLE_EQ(entry.duration, 0.25);
  EXPECT_EQ(entry.encoded_bytes, 42);
  EXPECT_EQ(log.Find(1), &entry);
  EXPECT_EQ(log.Find(2), nullptr);
}

TEST(RequestLogTest, RedirectsKeepEachHopAndFailuresAreRecorded) {
  RequestLog log;
  log.OnRequest("7", "http://a/", "GET", "Document", {}, std::nullopt, 1, 1);
  log.OnRequest("7", "https://a/", "GET", "Document", {}, std::nullopt, 1, 1.1);
  log.OnFailed("7", 2, "net::ERR_ABORTED");
  ASSERT_EQ(log.entries().size(), 2u);
  EXPECT_EQ(log.entries()[0].error, "");
  EXPECT_EQ(log.entries()[1].error, "net::ERR_ABORTED");
  EXPECT_TRUE(log.entries()[1].finished);
}

TEST(RequestLogTest, KeepsTheNewestAndLimitsSizes) {
  RequestLog log;
  for (std::size_t i = 0; i < RequestLog::kMaxEntries + 5; ++i) {
    log.OnRequest(std::to_string(i), "https://a/", "GET", "XHR", {},
                  std::nullopt, 0, 0);
  }
  EXPECT_EQ(log.entries().size(), RequestLog::kMaxEntries);
  EXPECT_EQ(log.entries().front().id, "5");
  log.OnRequest("big", "https://a/", "POST", "XHR",
                {{"X", std::string(70000, 'x')}},
                std::string(RequestLog::kMaxPostDataBytes + 10, 'y'), 0, 0);
  EXPECT_TRUE(log.entries().back().request_headers.empty());
  EXPECT_EQ(log.entries().back().post_data->size(),
            RequestLog::kMaxPostDataBytes);
  EXPECT_TRUE(log.entries().back().post_data_truncated);
  std::uint64_t version = log.version();
  log.Clear();
  EXPECT_TRUE(log.entries().empty());
  EXPECT_GT(log.version(), version);
}

TEST(RequestLogTest, FindsJwts) {
  EXPECT_EQ(FindJwts(std::string("Bearer ") + kJwt),
            std::vector<std::string>{kJwt});
  EXPECT_TRUE(FindJwts("no tokens here").empty());
  EXPECT_TRUE(FindJwts("xeyJabc.def.ghi1234567890").empty());  // Inside a word.
  EXPECT_TRUE(FindJwts("eyJa..b").empty());
  EXPECT_EQ(FindJwts(std::string("a=") + kJwt + "; b=" + kJwt).size(), 2u);
}

TEST(RequestLogTest, FindTokensNewestFirstAndOnce) {
  RequestLog log;
  log.OnRequest("1", "https://api/one", "GET", "XHR",
                {{"Authorization", std::string("Bearer ") + kJwt}},
                std::nullopt, 0, 0);
  log.OnRequest("2", "https://api/two", "GET", "XHR",
                {{"Authorization", std::string("Bearer ") + kJwt}},
                std::nullopt, 0, 0);
  log.OnRequest("3", "https://api/three", "GET", "XHR",
                {{"Authorization", "Bearer opaque-token"}}, std::nullopt, 0, 0);
  std::vector<FoundToken> tokens = FindTokens(log, 10);
  ASSERT_EQ(tokens.size(), 2u);
  EXPECT_EQ(tokens[0].token, "opaque-token");
  EXPECT_EQ(tokens[1].where,
            "Request header Authorization: GET https://api/two");
}

}  // namespace
}  // namespace brouk
