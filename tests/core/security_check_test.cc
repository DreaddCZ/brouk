#include "core/security_check.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

void Add(RequestLog& log,
         const std::string& id,
         const std::string& url,
         const std::string& type,
         int status = 200) {
  log.OnRequest(id, url, "GET", type, {}, std::nullopt, 1, 1);
  if (status > 0) {
    log.OnResponse(id, status, "text/html", {{"X-Id", id}});
  }
}

TEST(SecurityCheckTest, NoDocumentWhenThePageLoadWasNotRecorded) {
  RequestLog log;
  Add(log, "1", "https://example.com/api", "Fetch");
  SecurityCheckRequests found =
      CollectSecurityCheckRequests(log, "https://example.com/");
  EXPECT_EQ(found.document, nullptr);
  EXPECT_TRUE(found.site_requests.empty());
  EXPECT_TRUE(found.page_requests.empty());
}

TEST(SecurityCheckTest, PicksThePagesLastLoadAndItsSiteRequests) {
  RequestLog log;
  Add(log, "1", "https://example.com/app", "Document");
  Add(log, "2", "https://example.com/old-api", "Fetch");
  // Reloaded: only what came after the last load counts.
  Add(log, "3", "https://example.com/app", "Document");
  Add(log, "4", "https://api.example.com/v1/me", "XHR");
  Add(log, "5", "https://cdn.example.net/lib.js", "Script");
  Add(log, "6", "https://other.example.net/api", "Fetch");
  Add(log, "7", "https://example.com/pending", "Fetch", 0);
  Add(log, "8", "http://example.com/img.png", "Image");
  SecurityCheckRequests found =
      CollectSecurityCheckRequests(log, "https://example.com/app#section");
  ASSERT_NE(found.document, nullptr);
  EXPECT_EQ(found.document->seq, 3u);
  ASSERT_EQ(found.site_requests.size(), 1u);
  EXPECT_EQ(found.site_requests[0]->url, "https://api.example.com/v1/me");
  ASSERT_EQ(found.page_requests.size(), 6u);
  EXPECT_EQ(found.page_requests.front()->seq, 3u);
  EXPECT_EQ(found.page_requests.back()->url, "http://example.com/img.png");
}

TEST(SecurityCheckTest, RedirectedLoadUsesTheLastHop) {
  RequestLog log;
  log.OnRequest("1", "http://example.com/", "GET", "Document", {}, std::nullopt,
                1, 1);
  log.OnResponse("1", 301, "text/html", {});
  log.OnRequest("1", "https://example.com/", "GET", "Document", {},
                std::nullopt, 1, 1.1);
  log.OnResponse("1", 200, "text/html", {{"Server", "x"}});
  SecurityCheckRequests found =
      CollectSecurityCheckRequests(log, "https://example.com/");
  ASSERT_NE(found.document, nullptr);
  EXPECT_EQ(found.document->url, "https://example.com/");
  EXPECT_EQ(found.document->status, 200);
}

TEST(SecurityCheckTest, KeepsOnlyTheNewestSiteRequests) {
  RequestLog log;
  Add(log, "d", "https://example.com/", "Document");
  for (int i = 0; i < 250; ++i) {
    Add(log, "r" + std::to_string(i),
        "https://example.com/api/" + std::to_string(i), "Fetch");
  }
  SecurityCheckRequests found =
      CollectSecurityCheckRequests(log, "https://example.com/");
  ASSERT_EQ(found.site_requests.size(),
            SecurityCheckRequests::kMaxSiteRequests);
  EXPECT_EQ(found.site_requests.back()->url, "https://example.com/api/249");
  EXPECT_EQ(found.page_requests.size(), 251u);
}

TEST(SecurityCheckTest, Site) {
  EXPECT_EQ(SecurityCheckSite("https://login.example.co.uk/a?b#c"),
            "example.co.uk");
  EXPECT_EQ(SecurityCheckSite("http://localhost:8080/"), "localhost");
  EXPECT_EQ(SecurityCheckSite("brouk://tools/"), "");
}

TEST(SecurityCheckTest, FindingIds) {
  EXPECT_TRUE(IsFindingId("cookie-httponly:session"));
  EXPECT_FALSE(IsFindingId(""));
  EXPECT_FALSE(IsFindingId("has space"));
  EXPECT_FALSE(IsFindingId("line\nbreak"));
  EXPECT_FALSE(IsFindingId(std::string(201, 'a')));
}

TEST(SecurityCheckTest, HiddenFindingsFileRoundTrip) {
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "brouk-security-check-test";
  std::filesystem::remove_all(dir);
  const std::filesystem::path path = dir / "security-check.yaml";
  EXPECT_TRUE(LoadHiddenFindings(path).empty());
  HiddenFindings hidden = {
      {"example.com", {"csp-missing", "cookie-httponly:session"}},
      {"example.org", {}},
  };
  ASSERT_FALSE(SaveHiddenFindings(path, hidden));
  HiddenFindings back = LoadHiddenFindings(path);
  ASSERT_EQ(back.size(), 1u);
  EXPECT_EQ(
      back["example.com"],
      (std::vector<std::string>{"csp-missing", "cookie-httponly:session"}));
  std::filesystem::remove_all(dir);
}

TEST(SecurityCheckTest, HiddenFindingsFileIgnoresBadEntries) {
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "brouk-security-check-bad";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  const std::filesystem::path path = dir / "security-check.yaml";
  {
    std::ofstream file(path);
    file << "sites:\n"
            "  example.com:\n"
            "    - csp-missing\n"
            "    - csp-missing\n"
            "    - \"has space\"\n"
            "    - [nested]\n"
            "  example.org: not-a-list\n";
  }
  HiddenFindings back = LoadHiddenFindings(path);
  ASSERT_EQ(back.size(), 1u);
  EXPECT_EQ(back["example.com"], std::vector<std::string>{"csp-missing"});
  {
    std::ofstream file(path);
    file << "sites: [unclosed\n";
  }
  EXPECT_TRUE(LoadHiddenFindings(path).empty());
  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace brouk
