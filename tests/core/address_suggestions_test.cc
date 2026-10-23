#include "core/address_suggestions.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/search_engines.h"
#include "core/settings.h"

namespace brouk {
namespace {

using Kind = AddressSuggestion::Kind;
constexpr std::string_view kGoogle = "https://www.google.com/search?q=%s";

HistoryMatch Page(std::string url,
                  std::string title,
                  std::string completion = "") {
  return {.url = std::move(url),
          .title = std::move(title),
          .inline_completion = std::move(completion)};
}

std::vector<AddressSuggestion> Build(std::string_view text,
                                     const std::vector<HistoryMatch>& history,
                                     const std::vector<std::string>& search) {
  return BuildAddressSuggestions({.text = text,
                                  .search_template = kGoogle,
                                  .search_engine_name = "Google",
                                  .history = history,
                                  .search_suggestions = search});
}

TEST(AddressSuggestionsTest, EmptyTextHasNoRows) {
  EXPECT_TRUE(Build("  ", {Page("https://a.com/", "A")}, {"a"}).empty());
}

TEST(AddressSuggestionsTest, TypedRowThenHistoryThenSearch) {
  std::vector<AddressSuggestion> rows =
      Build("brouk", {Page("https://github.com/brouk", "Brouk repository")},
            {"brouk", "brouk v hlavě", "Brouk V Hlavě", "brouk pytlík"});
  ASSERT_EQ(rows.size(), 4U);
  EXPECT_EQ(rows[0].kind, Kind::kTypedSearch);
  EXPECT_EQ(rows[0].text, "brouk");
  EXPECT_EQ(rows[0].detail, "Search Google");
  EXPECT_EQ(rows[0].url, "https://www.google.com/search?q=brouk");
  EXPECT_EQ(rows[1].kind, Kind::kHistory);
  EXPECT_EQ(rows[1].text, "Brouk repository");
  EXPECT_EQ(rows[1].detail, "github.com/brouk");
  EXPECT_EQ(rows[1].url, "https://github.com/brouk");
  // The typed text and the duplicate (other case) are left out.
  EXPECT_EQ(rows[2].kind, Kind::kSearch);
  EXPECT_EQ(rows[2].text, "brouk v hlavě");
  EXPECT_EQ(rows[2].url, "https://www.google.com/search?q=brouk+v+hlav%C4%9B");
  EXPECT_EQ(rows[3].text, "brouk pytlík");
}

TEST(AddressSuggestionsTest, InlineCompletedPageComesFirst) {
  std::vector<AddressSuggestion> rows =
      Build("git",
            {Page("https://github.com/", "GitHub", "hub.com/"),
             Page("https://gitlab.com/", "GitLab")},
            {});
  ASSERT_EQ(rows.size(), 3U);
  EXPECT_EQ(rows[0].kind, Kind::kHistory);
  EXPECT_EQ(rows[0].inline_completion, "hub.com/");
  EXPECT_EQ(rows[1].text, "GitLab");
  EXPECT_EQ(rows[2].kind, Kind::kTypedSearch);  // Still in the list.
}

TEST(AddressSuggestionsTest, TypedAddressAndItsHistoryPageAreOneRow) {
  std::vector<AddressSuggestion> rows =
      Build("example.com",
            {Page("https://example.com/", "Example"),
             Page("https://example.com/a", "A")},
            {});
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0].kind, Kind::kTypedUrl);
  EXPECT_EQ(rows[0].url, "https://example.com");
  EXPECT_EQ(rows[1].url, "https://example.com/a");
}

TEST(AddressSuggestionsTest, LimitsEachSource) {
  std::vector<HistoryMatch> history;
  for (int i = 0; i < 9; ++i) {
    history.push_back(Page("https://x.com/" + std::to_string(i), "X"));
  }
  std::vector<std::string> search = {"x1", "x2", "x3", "x4", "x5", "x6"};
  std::vector<AddressSuggestion> rows = Build("x", history, search);
  EXPECT_EQ(rows.size(), 1 + kMaxHistorySuggestions + kMaxSearchSuggestions);
}

TEST(AddressSuggestionsTest, BookmarksComeAfterHistory) {
  std::vector<HistoryMatch> history = {Page("https://jira.example/", "Jira")};
  std::vector<BookmarkMatch> bookmarks = {
      {.name = "Jira", .url = "https://jira.example"},  // Same page.
      {.name = "Board", .url = "https://jira.example/board"},
      {.name = "", .url = "https://jira.example/a"},
      {.name = "B", .url = "https://jira.example/b"},
      {.name = "C", .url = "https://jira.example/c"},
  };
  std::vector<std::string> search = {"jira login"};
  std::vector<AddressSuggestion> rows =
      BuildAddressSuggestions({.text = "jira",
                               .search_template = kGoogle,
                               .search_engine_name = "Google",
                               .history = history,
                               .search_suggestions = search,
                               .bookmarks = bookmarks});
  ASSERT_EQ(rows.size(), 6U);
  EXPECT_EQ(rows[0].kind, Kind::kTypedSearch);
  EXPECT_EQ(rows[1].kind, Kind::kHistory);
  EXPECT_EQ(rows[2].kind, Kind::kBookmark);
  EXPECT_EQ(rows[2].text, "Board");
  EXPECT_EQ(rows[2].url, "https://jira.example/board");
  EXPECT_EQ(rows[3].text, "jira.example/a") << "no name: the address";
  EXPECT_EQ(rows[4].text, "B");
  EXPECT_EQ(rows[5].kind, Kind::kSearch);
}

TEST(AddressSuggestionsTest, WhatIsSentToTheSearchEngine) {
  EXPECT_TRUE(ShouldFetchSearchSuggestions("brouk"));
  EXPECT_TRUE(ShouldFetchSearchSuggestions("github.com/brouk"));
  EXPECT_FALSE(ShouldFetchSearchSuggestions("   "));
  EXPECT_FALSE(ShouldFetchSearchSuggestions("https://intranet/secret"));
  EXPECT_FALSE(ShouldFetchSearchSuggestions("file:///Users/me"));
  EXPECT_FALSE(ShouldFetchSearchSuggestions("javascript:alert(1)"));
  EXPECT_FALSE(ShouldFetchSearchSuggestions(std::string(300, 'a')));
}

TEST(AddressSuggestionsTest, SuggestionServices) {
  Settings global(SettingScope::kGlobal);
  EXPECT_EQ(SearchSuggestTemplate(global),
            "https://www.google.com/complete/search?client=chrome&q=%s");
  global.Set("search.engine", std::string("duckduckgo"));
  EXPECT_EQ(SearchSuggestTemplate(global),
            "https://duckduckgo.com/ac/?q=%s&type=list");
  global.Set("search.engine", std::string("custom"));
  global.Set("search.custom_template",
             std::string("https://search.example.com/?q=%s"));
  EXPECT_EQ(SearchSuggestTemplate(global), "");
  global.Set("search.engine", std::string("bing"));
  global.Set("suggestions.search", false);
  EXPECT_EQ(SearchSuggestTemplate(global), "");
}

}  // namespace
}  // namespace brouk
