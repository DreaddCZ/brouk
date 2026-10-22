#ifndef BROUK_CORE_SEARCH_ENGINES_H_
#define BROUK_CORE_SEARCH_ENGINES_H_

#include <string>

#include "core/settings.h"

namespace brouk {

// The search URL template (with %s) chosen in the global settings
// (search.engine, search.custom_template; 003). "custom" without a usable
// template falls back to Google.
std::string SearchTemplate(const Settings& global);

}  // namespace brouk

#endif  // BROUK_CORE_SEARCH_ENGINES_H_
