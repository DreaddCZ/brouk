#include "core/command_palette.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace brouk {

namespace {

// Base letters of U+00C0 ... U+017F (Latin-1 Supplement and Latin
// Extended-A), lowercase; "?" where there is none.
constexpr std::string_view kLatinBase =
    "aaaaaaaceeeeiiiidnoooooxouuuuytsaaaaaaaceeeeiiiidnooooo/ouuuuyty"
    "aaaaaaccccccccddddeeeeeeeeeegggggggghhhhiiiiiiiiiiiijjkkkllllllll"
    "llnnnnnnnnnoooooooorrrrrrssssssssttttttuuuuuuuuuuuuwwyyyzzzzzzs";

bool IsWordChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

// How well `word` matches `text` (both folded); nullopt if it does not.
std::optional<int> WordScore(std::string_view word,
                             std::string_view title,
                             std::string_view detail) {
  int best = 0;
  auto at_word_start = [](std::string_view text, std::size_t pos) {
    return pos == 0 || !IsWordChar(text[pos - 1]);
  };
  if (title.starts_with(word)) {
    return 100;
  }
  for (std::size_t pos = title.find(word); pos != std::string_view::npos;
       pos = title.find(word, pos + 1)) {
    best = std::max(best, at_word_start(title, pos) ? 80 : 50);
  }
  if (best > 0) {
    return best;
  }
  for (std::size_t pos = detail.find(word); pos != std::string_view::npos;
       pos = detail.find(word, pos + 1)) {
    best = std::max(best, at_word_start(detail, pos) ? 30 : 20);
  }
  if (best > 0) {
    return best;
  }
  // The letters in order, e.g. "nt" in "new tab".
  if (word.size() >= 2) {
    std::size_t next = 0;
    for (char c : title) {
      if (next < word.size() && c == word[next]) {
        ++next;
      }
    }
    if (next == word.size()) {
      return 10;
    }
  }
  return std::nullopt;
}

std::vector<std::string> Words(std::string_view folded) {
  std::vector<std::string> words;
  std::string current;
  for (char c : folded) {
    if (c == ' ' || c == '\t') {
      if (!current.empty()) {
        words.push_back(std::move(current));
        current.clear();
      }
    } else {
      current += c;
    }
  }
  if (!current.empty()) {
    words.push_back(std::move(current));
  }
  return words;
}

}  // namespace

std::string FoldForSearch(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    auto c = static_cast<unsigned char>(text[i]);
    // Two-byte UTF-8 sequences C3 80 ... C5 BF are U+00C0 ... U+017F.
    if (c >= 0xC3 && c <= 0xC5 && i + 1 < text.size()) {
      auto next = static_cast<unsigned char>(text[i + 1]);
      if ((next & 0xC0) == 0x80) {
        unsigned code = ((c & 0x1Fu) << 6) | (next & 0x3Fu);
        if (code >= 0xC0 && code - 0xC0 < kLatinBase.size() &&
            kLatinBase[code - 0xC0] != '?') {
          result += kLatinBase[code - 0xC0];
          ++i;
          continue;
        }
      }
    }
    result += static_cast<char>(c < 0x80 ? std::tolower(c) : c);
  }
  return result;
}

std::vector<std::size_t> RankPaletteItems(std::span<const PaletteItem> items,
                                          std::string_view query,
                                          std::size_t limit) {
  std::vector<std::string> words = Words(FoldForSearch(query));
  struct Match {
    std::size_t index;
    int score;
  };
  std::vector<Match> matches;
  for (std::size_t i = 0; i < items.size(); ++i) {
    std::string title = FoldForSearch(items[i].title);
    std::string detail = FoldForSearch(items[i].detail);
    int total = 0;
    bool all = true;
    for (const std::string& word : words) {
      std::optional<int> score = WordScore(word, title, detail);
      if (!score) {
        all = false;
        break;
      }
      total += *score;
    }
    if (all) {
      matches.push_back({i, total});
    }
  }
  if (!words.empty()) {
    std::ranges::stable_sort(matches, [&items](const Match& a, const Match& b) {
      if (a.score != b.score) {
        return a.score > b.score;
      }
      if (items[a.index].kind != items[b.index].kind) {
        return items[a.index].kind < items[b.index].kind;
      }
      return items[a.index].title.size() < items[b.index].title.size();
    });
  }
  std::vector<std::size_t> result;
  for (std::size_t i = 0; i < matches.size() && i < limit; ++i) {
    result.push_back(matches[i].index);
  }
  return result;
}

}  // namespace brouk
