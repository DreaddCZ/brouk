#include "core/context_menu.h"

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

using C = MenuCommand;

std::vector<C> Commands(const std::vector<MenuItem>& items) {
  std::vector<C> out;
  for (const MenuItem& item : items) {
    out.push_back(item.command);
  }
  return out;
}

TEST(ContextMenuTest, PageMenu) {
  ContextMenuContext context;
  context.can_go_back = true;
  std::vector<MenuItem> menu = BuildContextMenu(context);
  EXPECT_EQ(Commands(menu),
            (std::vector<C>{C::kBack, C::kForward, C::kReload, C::kSeparator,
                            C::kPrint, C::kSeparator, C::kViewSource,
                            C::kSeparator, C::kInspect}));
  EXPECT_TRUE(menu[0].enabled);
  EXPECT_FALSE(menu[1].enabled);
}

TEST(ContextMenuTest, LinkMenu) {
  ContextMenuContext context;
  context.has_link = true;
  EXPECT_EQ(Commands(BuildContextMenu(context)),
            (std::vector<C>{C::kOpenLinkInNewTab, C::kOpenLinkInNewWindow,
                            C::kSeparator, C::kSaveLinkAs, C::kCopyLinkAddress,
                            C::kSeparator, C::kInspect}));
}

TEST(ContextMenuTest, LinkedImageHasBothSections) {
  ContextMenuContext context;
  context.has_link = true;
  context.has_image = true;
  std::vector<C> commands = Commands(BuildContextMenu(context));
  EXPECT_EQ(commands.front(), C::kOpenLinkInNewTab);
  EXPECT_NE(std::find(commands.begin(), commands.end(), C::kCopyImage),
            commands.end());
  EXPECT_EQ(commands.back(), C::kInspect);
}

TEST(ContextMenuTest, SelectionMenu) {
  ContextMenuContext context;
  context.selection_text = "hello";
  std::vector<MenuItem> menu = BuildContextMenu(context);
  EXPECT_EQ(Commands(menu),
            (std::vector<C>{C::kCopy, C::kSearchForSelection, C::kPrint,
                            C::kSeparator, C::kInspect}));
  EXPECT_EQ(menu[1].label, "Search Google for “hello”");
}

TEST(ContextMenuTest, EditableMenuReflectsEditState) {
  ContextMenuContext context;
  context.is_editable = true;
  context.can_paste = true;
  context.can_select_all = true;
  std::vector<MenuItem> menu = BuildContextMenu(context);
  EXPECT_EQ(Commands(menu),
            (std::vector<C>{C::kUndo, C::kRedo, C::kSeparator, C::kCut,
                            C::kCopy, C::kPaste, C::kPasteAndMatchStyle,
                            C::kSelectAll, C::kSeparator, C::kInspect}));
  EXPECT_FALSE(menu[0].enabled);
  EXPECT_FALSE(menu[3].enabled);
  EXPECT_TRUE(menu[5].enabled);
}

TEST(ContextMenuTest, MisspelledWordShowsSuggestionsFirst) {
  ContextMenuContext context;
  context.is_editable = true;
  context.misspelled_word = "brouc";
  context.spelling_suggestions = {"brouk", "brouci"};
  std::vector<MenuItem> menu = BuildContextMenu(context);
  ASSERT_GE(menu.size(), 4u);
  EXPECT_EQ(menu[0].command, C::kSpellingSuggestion);
  EXPECT_EQ(menu[0].label, "brouk");
  EXPECT_EQ(menu[1].index, 1);
  EXPECT_EQ(menu[2].command, C::kAddToDictionary);
  EXPECT_EQ(menu[3].command, C::kSeparator);
}

TEST(ContextMenuTest, MisspelledWordWithoutSuggestions) {
  ContextMenuContext context;
  context.is_editable = true;
  context.misspelled_word = "xqzt";
  std::vector<MenuItem> menu = BuildContextMenu(context);
  EXPECT_EQ(menu[0].label, "No Guesses Found");
  EXPECT_FALSE(menu[0].enabled);
}

TEST(ContextMenuTest, NoDoubledOrTrailingSeparators) {
  for (int mask = 0; mask < 16; ++mask) {
    ContextMenuContext context;
    context.has_link = mask & 1;
    context.has_image = mask & 2;
    context.is_editable = mask & 4;
    context.selection_text = (mask & 8) ? "x" : "";
    std::vector<MenuItem> menu = BuildContextMenu(context);
    ASSERT_FALSE(menu.empty());
    EXPECT_NE(menu.front().command, C::kSeparator);
    EXPECT_NE(menu.back().command, C::kSeparator);
    for (std::size_t i = 1; i < menu.size(); ++i) {
      EXPECT_FALSE(menu[i].command == C::kSeparator &&
                   menu[i - 1].command == C::kSeparator)
          << "mask " << mask;
    }
  }
}

TEST(ContextMenuTest, InternalPageMenu) {
  ContextMenuContext context;
  context.can_go_back = true;
  context.has_link = true;
  context.has_image = true;
  // Links and images of internal pages get no web commands.
  EXPECT_EQ(Commands(BuildInternalPageContextMenu(context)),
            (std::vector<C>{C::kReload, C::kSeparator, C::kInspect}));
}

TEST(ContextMenuTest, InternalPageSelectionMenu) {
  ContextMenuContext context;
  context.selection_text = "hello";
  EXPECT_EQ(Commands(BuildInternalPageContextMenu(context)),
            (std::vector<C>{C::kCopy, C::kSeparator, C::kInspect}));
}

TEST(ContextMenuTest, InternalPageEditableMenu) {
  ContextMenuContext context;
  context.is_editable = true;
  context.selection_text = "hello";
  context.can_copy = true;
  std::vector<MenuItem> menu = BuildInternalPageContextMenu(context);
  EXPECT_EQ(
      Commands(menu),
      (std::vector<C>{C::kUndo, C::kRedo, C::kSeparator, C::kCut, C::kCopy,
                      C::kPaste, C::kSelectAll, C::kSeparator, C::kInspect}));
  EXPECT_FALSE(menu[0].enabled);
  EXPECT_TRUE(menu[4].enabled);
}

TEST(ContextMenuTest, SearchLabelCollapsesAndShortens) {
  EXPECT_EQ(SearchSelectionLabel("Google", "  a \n\t b  "),
            "Search Google for “a b”");
  std::string long_text(60, 'x');
  EXPECT_EQ(SearchSelectionLabel("Google", long_text),
            "Search Google for “" + std::string(50, 'x') + "…”");
  // Multi-byte characters are never cut in half.
  std::string czech;
  for (int i = 0; i < 60; ++i) {
    czech += "ž";
  }
  std::string expected;
  for (int i = 0; i < 50; ++i) {
    expected += "ž";
  }
  EXPECT_EQ(SearchSelectionLabel("Google", czech),
            "Search Google for “" + expected + "…”");
}

TEST(ContextMenuTest, BlockElementWhenAdBlockingApplies) {
  ContextMenuContext context;
  context.can_block_element = true;
  std::vector<MenuItem> menu = BuildContextMenu(context);
  ASSERT_GE(menu.size(), 2U);
  EXPECT_EQ(menu[menu.size() - 2].command, MenuCommand::kBlockElement);
  EXPECT_EQ(menu.back().command, MenuCommand::kInspect);
  context.is_editable = true;
  for (const MenuItem& item : BuildContextMenu(context)) {
    EXPECT_NE(item.command, MenuCommand::kBlockElement);
  }
}

}  // namespace
}  // namespace brouk
