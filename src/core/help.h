#ifndef BROUK_CORE_HELP_H_
#define BROUK_CORE_HELP_H_

#include <string_view>

namespace brouk {

// Whether `anchor` can name a section of the help page (016): 1 to 64
// lowercase letters, digits and '-', so a page or link cannot pass anything
// else into the help window's address.
bool IsHelpAnchor(std::string_view anchor);

}  // namespace brouk

#endif  // BROUK_CORE_HELP_H_
