#include "core/password_checkup.h"

#include <charconv>
#include <map>

#include "core/crypto.h"

namespace brouk {

PwnedQuery MakePwnedQuery(std::string_view password) {
  std::string hash = HexEncode(Sha1(password));
  return {hash.substr(0, 5), hash.substr(5)};
}

std::int64_t PwnedCount(std::string_view response, std::string_view suffix) {
  while (!response.empty()) {
    std::size_t end = response.find('\n');
    std::string_view line = response.substr(0, end);
    response.remove_prefix(end == std::string_view::npos ? response.size()
                                                         : end + 1);
    if (line.ends_with('\r')) {
      line.remove_suffix(1);
    }
    std::size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon != suffix.size()) {
      continue;
    }
    bool same = true;
    for (std::size_t i = 0; i < colon; ++i) {
      char a = line[i];
      char b = suffix[i];
      if (a >= 'a' && a <= 'f') {
        a = static_cast<char>(a - 'a' + 'A');
      }
      if (a != b) {
        same = false;
        break;
      }
    }
    if (!same) {
      continue;
    }
    std::int64_t count = 0;
    std::string_view number = line.substr(colon + 1);
    std::from_chars(number.data(), number.data() + number.size(), count);
    return count;
  }
  return 0;
}

std::vector<std::vector<std::size_t>> FindReusedPasswords(
    std::span<const std::string> passwords) {
  std::map<std::string_view, std::size_t> group_of;
  std::vector<std::vector<std::size_t>> groups;
  for (std::size_t i = 0; i < passwords.size(); ++i) {
    if (passwords[i].empty()) {
      continue;
    }
    auto [it, added] = group_of.try_emplace(passwords[i], groups.size());
    if (added) {
      groups.emplace_back();
    }
    groups[it->second].push_back(i);
  }
  std::vector<std::vector<std::size_t>> reused;
  for (std::vector<std::size_t>& group : groups) {
    if (group.size() > 1) {
      reused.push_back(std::move(group));
    }
  }
  return reused;
}

}  // namespace brouk
