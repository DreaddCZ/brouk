#include "core/url_display.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace brouk {

namespace {

bool StartsWithIgnoreCase(std::string_view s, std::string_view prefix) {
  if (s.size() < prefix.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    char a = s[i];
    if (a >= 'A' && a <= 'Z') {
      a = static_cast<char>(a - 'A' + 'a');
    }
    if (a != prefix[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::string FormatUrlForDisplay(std::string_view url) {
  if (url.empty() || url == "about:blank") {
    return {};
  }
  std::string_view rest;
  if (StartsWithIgnoreCase(url, "https://")) {
    rest = url.substr(8);
  } else if (StartsWithIgnoreCase(url, "http://")) {
    rest = url.substr(7);
  } else {
    return std::string(url);
  }
  // Keep user:password@ visible: hiding parts of such URLs could mislead.
  std::size_t authority_end = rest.find_first_of("/?#");
  if (rest.substr(0, authority_end).find('@') != std::string_view::npos) {
    return std::string(url);
  }
  if (StartsWithIgnoreCase(rest, "www.") && rest.size() > 4) {
    rest.remove_prefix(4);
  }
  // "example.com/" -> "example.com", but keep "example.com/?q" intact.
  if (rest.size() > 1 && rest.back() == '/' &&
      rest.find('/') == rest.size() - 1) {
    rest.remove_suffix(1);
  }
  return std::string(rest);
}

}  // namespace brouk
