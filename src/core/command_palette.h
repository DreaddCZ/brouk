#ifndef BROUK_CORE_COMMAND_PALETTE_H_
#define BROUK_CORE_COMMAND_PALETTE_H_

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace brouk {

// One thing the command palette (003) can find.
struct PaletteItem {
  // Also the order of equally good matches.
  enum class Kind { kTab, kAction, kBookmark, kSetting };
  Kind kind = Kind::kAction;
  std::string title;   // Matched first: a tab's title, an action's name…
  std::string detail;  // Matched too, worth less: a tab's address, a section.
};

// Lowercase, with accents removed from Latin letters ("Nastavení" ->
// "nastaveni"), so a query typed without accents still matches.
std::string FoldForSearch(std::string_view text);

// The indices of `items` that match `query`, best first, at most `limit`.
// Every word of the query must appear in the title or the detail. Words at
// the start of the title or of a word in it count most, then anywhere in the
// title, then in the detail; a title also matches if the word's letters
// appear in it in order ("nt" -> "New Tab"), which counts least. An empty
// query keeps the items' order.
std::vector<std::size_t> RankPaletteItems(std::span<const PaletteItem> items,
                                          std::string_view query,
                                          std::size_t limit);

}  // namespace brouk

#endif  // BROUK_CORE_COMMAND_PALETTE_H_
