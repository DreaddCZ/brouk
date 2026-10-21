#include "core/address_input.h"

#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

using Kind = AddressInputResult::Kind;

std::string Url(const char* input) {
  AddressInputResult result = ClassifyAddressInput(input);
  EXPECT_EQ(result.kind, Kind::kUrl) << input;
  return result.url;
}

std::string Search(const char* input) {
  AddressInputResult result = ClassifyAddressInput(input);
  EXPECT_EQ(result.kind, Kind::kSearch) << input;
  return result.url;
}

TEST(AddressInputTest, EmptyInputLoadsNothing) {
  EXPECT_EQ(ClassifyAddressInput("").kind, Kind::kEmpty);
  EXPECT_EQ(ClassifyAddressInput("  \t ").kind, Kind::kEmpty);
}

TEST(AddressInputTest, FullUrlsAreKept) {
  EXPECT_EQ(Url("https://example.com/a?b=c#d"), "https://example.com/a?b=c#d");
  EXPECT_EQ(Url("http://intranet/"), "http://intranet/");
  EXPECT_EQ(Url("  https://example.com  "), "https://example.com");
  EXPECT_EQ(Url("HTTPS://Example.com"), "HTTPS://Example.com");
}

TEST(AddressInputTest, KnownSchemesWithoutSlashesAreUrls) {
  EXPECT_EQ(Url("about:blank"), "about:blank");
  EXPECT_EQ(Url("mailto:someone@example.com"), "mailto:someone@example.com");
  EXPECT_EQ(Url("file:///Users/me/a.pdf"), "file:///Users/me/a.pdf");
  EXPECT_EQ(Url("data:text/plain,hi"), "data:text/plain,hi");
}

TEST(AddressInputTest, UnknownSchemeWithSlashesIsUrl) {
  EXPECT_EQ(Url("msteams://teams.microsoft.com/l/x"),
            "msteams://teams.microsoft.com/l/x");
}

TEST(AddressInputTest, JavascriptUrlIsSearched) {
  Search("javascript:alert(1)");
}

TEST(AddressInputTest, DomainsGetHttps) {
  EXPECT_EQ(Url("example.com"), "https://example.com");
  EXPECT_EQ(Url("www.example.co.uk/path?q=1"),
            "https://www.example.co.uk/path?q=1");
  EXPECT_EQ(Url("example.com:8443/x"), "https://example.com:8443/x");
  EXPECT_EQ(Url("example.com."), "https://example.com.");
  EXPECT_EQ(Url("xn--bcher-kva.example"), "https://xn--bcher-kva.example");
  EXPECT_EQ(Url("my-site.dev#top"), "https://my-site.dev#top");
}

TEST(AddressInputTest, LocalHostsGetHttp) {
  EXPECT_EQ(Url("localhost"), "http://localhost");
  EXPECT_EQ(Url("localhost:3000/api"), "http://localhost:3000/api");
  EXPECT_EQ(Url("127.0.0.1:8080"), "http://127.0.0.1:8080");
  EXPECT_EQ(Url("192.168.1.1"), "http://192.168.1.1");
  EXPECT_EQ(Url("[::1]:8080/"), "http://[::1]:8080/");
}

TEST(AddressInputTest, NonUrlsAreSearched) {
  EXPECT_EQ(Search("hello world"),
            "https://www.google.com/search?q=hello+world");
  EXPECT_EQ(Search("hello"), "https://www.google.com/search?q=hello");
  Search("site:example.com");
  Search("someone@example.com");
  Search("1.5");
  Search("300.1.1.1");
  Search("example.com:99999");
  Search("example.c0m");
  Search("-bad-.com");
  Search("example.com is great");
}

TEST(AddressInputTest, SearchQueryIsEncoded) {
  EXPECT_EQ(Search("c++ & rust?"),
            "https://www.google.com/search?q=c%2B%2B+%26+rust%3F");
  EXPECT_EQ(Search("žluťoučký kůň"),
            "https://www.google.com/search?q=%C5%BElu%C5%A5ou%C4%8Dk%C3%BD+"
            "k%C5%AF%C5%88");
}

TEST(AddressInputTest, CustomSearchTemplate) {
  AddressInputResult result =
      ClassifyAddressInput("two words", "https://duckduckgo.com/?q=%s&ia=web");
  EXPECT_EQ(result.kind, Kind::kSearch);
  EXPECT_EQ(result.url, "https://duckduckgo.com/?q=two+words&ia=web");
}

TEST(AddressInputTest, EncodeQueryComponentKeepsUnreserved) {
  EXPECT_EQ(EncodeQueryComponent("aZ09-_.~"), "aZ09-_.~");
  EXPECT_EQ(EncodeQueryComponent("/?#%"), "%2F%3F%23%25");
}

}  // namespace
}  // namespace brouk
