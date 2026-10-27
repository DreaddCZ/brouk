#include "core/help.h"

#include <algorithm>
#include <string_view>

namespace brouk {

bool IsHelpAnchor(std::string_view anchor) {
  return !anchor.empty() && anchor.size() <= 64 &&
         std::ranges::all_of(anchor, [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
         });
}

}  // namespace brouk
