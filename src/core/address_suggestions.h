#ifndef BROUK_CORE_ADDRESS_SUGGESTIONS_H_
#define BROUK_CORE_ADDRESS_SUGGESTIONS_H_

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/history_store.h"

namespace brouk {

// One row of the address bar's suggestion list (003).
struct AddressSuggestion {
  enum class Kind {
    kTypedUrl,     // Go to the address as typed.
    kTypedSearch,  // Search for the text as typed.
    kHistory,      // A page from the profile's history.
    kBookmark,     // A bookmark visible in the tab's profile (011).
    kSearch,       // A search engine suggestion.
  };
  Kind kind = Kind::kTypedSearch;
  std::string text;    // The main text: an address, a title, a query.
  std::string detail;  // Below or after it: the address, "Search Google".
  std::string url;     // What Return loads.
  // Only on the first row: the rest of a typed-before address to complete
  // inline after the typed text.
  std::string inline_completion;
};

// A bookmark matching the typed text (011).
struct BookmarkMatch {
  std::string name;
  std::string url;
};

// What the list is built from.
struct AddressSuggestionInput {
  std::string_view text;  // As typed.
  std::string_view search_template;
  std::string_view search_engine_name;  // "Google"
  std::span<const HistoryMatch> history;
  std::span<const std::string> search_suggestions;
  std::span<const BookmarkMatch> bookmarks = {};
};

inline constexpr std::size_t kMaxHistorySuggestions = 5;
inline constexpr std::size_t kMaxSearchSuggestions = 4;
inline constexpr std::size_t kMaxBookmarkSuggestions = 3;

// The list in the order of 003: history first, then bookmarks (pages not
// already listed), then search suggestions. As in Chrome, the first row is what
// Return does: the history page completed inline, or else the typed text as an
// address or a search; the typed text keeps a row in the list either way. Empty
// for empty text.
std::vector<AddressSuggestion> BuildAddressSuggestions(
    const AddressSuggestionInput& input);

// Whether the text may be sent to the search engine for suggestions: not
// empty, not an address with a scheme ("https://…", "file:…"), which Chrome
// does not send either, and not longer than a search query would be.
bool ShouldFetchSearchSuggestions(std::string_view text);

// The suggestions of an OpenSearch answer, cleaned: no duplicates (ignoring
// case), not the typed text itself, at most `limit`.
std::vector<std::string> CleanSearchSuggestions(
    std::string_view typed,
    std::span<const std::string> suggestions,
    std::size_t limit);

}  // namespace brouk

#endif  // BROUK_CORE_ADDRESS_SUGGESTIONS_H_
