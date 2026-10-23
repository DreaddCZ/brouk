#include "core/address_suggestions.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/address_input.h"
#include "core/url_display.h"

namespace brouk {

namespace {

// Longer text is not sent to the search engine (not a query anyone types).
constexpr std::size_t kMaxSuggestQueryLength = 256;

std::string_view Trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return text;
}

std::string Lower(std::string_view text) {
  std::string result(text);
  for (char& c : result) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return result;
}

// "https://example.com" and "https://example.com/" are the same page.
bool SameAddress(std::string_view a, std::string_view b) {
  std::string left = UrlForMatching(a);
  std::string right = UrlForMatching(b);
  if (left.ends_with('/')) {
    left.pop_back();
  }
  if (right.ends_with('/')) {
    right.pop_back();
  }
  return left == right;
}

AddressSuggestion TypedRow(const AddressSuggestionInput& input) {
  std::string_view typed = Trim(input.text);
  AddressInputResult result =
      ClassifyAddressInput(typed, input.search_template);
  AddressSuggestion row;
  row.url = result.url;
  if (result.kind == AddressInputResult::Kind::kUrl) {
    row.kind = AddressSuggestion::Kind::kTypedUrl;
    row.text = FormatUrlForDisplay(result.url);
    if (row.text.empty()) {
      row.text = std::string(typed);
    }
  } else {
    row.kind = AddressSuggestion::Kind::kTypedSearch;
    row.text = std::string(typed);
    row.detail = "Search " + std::string(input.search_engine_name);
  }
  return row;
}

AddressSuggestion HistoryRow(const HistoryMatch& match) {
  AddressSuggestion row;
  row.kind = AddressSuggestion::Kind::kHistory;
  std::string address = FormatUrlForDisplay(match.url);
  row.text = match.title.empty() ? address : match.title;
  row.detail = match.title.empty() ? std::string() : address;
  row.url = match.url;
  return row;
}

}  // namespace

std::vector<AddressSuggestion> BuildAddressSuggestions(
    const AddressSuggestionInput& input) {
  std::vector<AddressSuggestion> rows;
  if (Trim(input.text).empty()) {
    return rows;
  }
  AddressSuggestion typed = TypedRow(input);
  std::span<const HistoryMatch> history = input.history.subspan(
      0, std::min(input.history.size(), kMaxHistorySuggestions));
  bool completes_inline =
      !history.empty() && !history[0].inline_completion.empty();
  if (!completes_inline) {
    rows.push_back(typed);
  }
  for (const HistoryMatch& match : history) {
    if (!completes_inline && typed.kind == AddressSuggestion::Kind::kTypedUrl &&
        SameAddress(match.url, typed.url)) {
      continue;  // Already the first row.
    }
    rows.push_back(HistoryRow(match));
  }
  if (completes_inline) {
    rows.front().inline_completion = history[0].inline_completion;
    rows.push_back(typed);
  }
  for (const std::string& suggestion :
       CleanSearchSuggestions(input.text, input.search_suggestions,
                              kMaxSearchSuggestions)) {
    AddressSuggestion row;
    row.kind = AddressSuggestion::Kind::kSearch;
    row.text = suggestion;
    row.detail = "Search " + std::string(input.search_engine_name);
    row.url = BuildSearchUrl(suggestion, input.search_template);
    rows.push_back(std::move(row));
  }
  return rows;
}

bool ShouldFetchSearchSuggestions(std::string_view text) {
  std::string_view typed = Trim(text);
  if (typed.empty() || typed.size() > kMaxSuggestQueryLength) {
    return false;
  }
  std::string lowered = Lower(typed);
  for (std::string_view scheme :
       {"file:", "about:", "data:", "javascript:", "blob:", "brouk:"}) {
    if (lowered.starts_with(scheme)) {
      return false;
    }
  }
  return lowered.find("://") == std::string::npos;
}

std::vector<std::string> CleanSearchSuggestions(
    std::string_view typed,
    std::span<const std::string> suggestions,
    std::size_t limit) {
  std::vector<std::string> result;
  std::set<std::string> seen = {Lower(Trim(typed))};
  for (const std::string& suggestion : suggestions) {
    std::string_view text = Trim(suggestion);
    if (result.size() >= limit) {
      break;
    }
    if (text.empty() || !seen.insert(Lower(text)).second) {
      continue;
    }
    result.emplace_back(text);
  }
  return result;
}

}  // namespace brouk
