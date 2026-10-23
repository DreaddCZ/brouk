#include "core/command_palette.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

using Kind = PaletteItem::Kind;

std::vector<std::string> Titles(const std::vector<PaletteItem>& items,
                                std::string_view query,
                                std::size_t limit = 10) {
  std::vector<std::string> titles;
  for (std::size_t index : RankPaletteItems(items, query, limit)) {
    titles.push_back(items[index].title);
  }
  return titles;
}

TEST(CommandPaletteTest, FoldsCaseAndAccents) {
  EXPECT_EQ(FoldForSearch("Nastavení"), "nastaveni");
  EXPECT_EQ(FoldForSearch("ŽLUŤOUČKÝ kůň úpěl ďábelské ódy"),
            "zlutoucky kun upel dabelske ody");
  EXPECT_EQ(FoldForSearch("Straße Øre Łódź"), "strase ore lodz");
  EXPECT_EQ(FoldForSearch("日本 €"), "日本 €");  // Left as they are.
}

TEST(CommandPaletteTest, EmptyQueryKeepsTheOrder) {
  std::vector<PaletteItem> items = {
      {Kind::kSetting, "Theme", ""},
      {Kind::kTab, "GitHub", "github.com"},
      {Kind::kAction, "New tab", ""},
  };
  EXPECT_EQ(Titles(items, ""),
            (std::vector<std::string>{"Theme", "GitHub", "New tab"}));
  EXPECT_EQ(Titles(items, "  ", 2),
            (std::vector<std::string>{"Theme", "GitHub"}));
}

TEST(CommandPaletteTest, PrefixBeatsWordStartBeatsSubstringBeatsDetail) {
  std::vector<PaletteItem> items = {
      {Kind::kAction, "Close tab", "tab"},
      {Kind::kAction, "Pin or unpin tab", ""},
      {Kind::kAction, "Tab 1", ""},
      {Kind::kTab, "Stable release notes", ""},
      {Kind::kSetting, "Theme", "Appearance"},
  };
  EXPECT_EQ(Titles(items, "tab"),
            (std::vector<std::string>{"Tab 1", "Close tab", "Pin or unpin tab",
                                      "Stable release notes"}));
}

TEST(CommandPaletteTest, EveryWordMustMatch) {
  std::vector<PaletteItem> items = {
      {Kind::kTab, "Pull requests", "github.com/brouk"},
      {Kind::kTab, "Pull requests", "gitlab.com/other"},
      {Kind::kAction, "Reload", ""},
  };
  EXPECT_EQ(Titles(items, "pull github"),
            (std::vector<std::string>{"Pull requests"}));
  EXPECT_EQ(RankPaletteItems(items, "pull github", 10),
            (std::vector<std::size_t>{0}));
  EXPECT_TRUE(Titles(items, "pull reload").empty());
}

TEST(CommandPaletteTest, LettersInOrderMatchTitlesOnly) {
  std::vector<PaletteItem> items = {
      {Kind::kAction, "New tab", ""},
      {Kind::kAction, "Reload", "network"},
  };
  EXPECT_EQ(Titles(items, "nt"), (std::vector<std::string>{"New tab"}));
  EXPECT_TRUE(Titles(items, "x").empty());
}

TEST(CommandPaletteTest, AccentsInTheQueryOrTheTitleDoNotMatter) {
  std::vector<PaletteItem> items = {
      {Kind::kTab, "Účetnictví – přehled", "intranet"},
  };
  EXPECT_EQ(Titles(items, "ucetnictvi").size(), 1U);
  EXPECT_EQ(Titles(items, "PŘEHLED").size(), 1U);
}

TEST(CommandPaletteTest, TiesGoToTabsThenActionsThenShorterTitles) {
  std::vector<PaletteItem> items = {
      {Kind::kSetting, "Downloads folder", ""},
      {Kind::kAction, "Downloads", ""},
      {Kind::kTab, "Downloads page", ""},
      {Kind::kAction, "Downloads list", ""},
  };
  EXPECT_EQ(Titles(items, "down"),
            (std::vector<std::string>{"Downloads page", "Downloads",
                                      "Downloads list", "Downloads folder"}));
  EXPECT_EQ(Titles(items, "down", 2).size(), 2U);
}

}  // namespace
}  // namespace brouk
