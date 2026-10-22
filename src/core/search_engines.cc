#include "core/search_engines.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "core/address_input.h"

namespace brouk {

namespace {

// The built-in list of 003.
constexpr std::array<std::pair<std::string_view, std::string_view>, 5>
    kEngines = {{
        {"google", kDefaultSearchTemplate},
        {"bing", "https://www.bing.com/search?q=%s"},
        {"duckduckgo", "https://duckduckgo.com/?q=%s"},
        {"startpage", "https://www.startpage.com/do/search?q=%s"},
        {"ecosia", "https://www.ecosia.org/search?q=%s"},
    }};

}  // namespace

std::string SearchTemplate(const Settings& global) {
  const std::string& engine = global.GetString("search.engine");
  if (engine == "custom") {
    // Validated when set: empty, or an http(s) URL with %s.
    const std::string& custom = global.GetString("search.custom_template");
    if (!custom.empty()) {
      return custom;
    }
  }
  for (const auto& [name, url] : kEngines) {
    if (name == engine) {
      return std::string(url);
    }
  }
  return std::string(kDefaultSearchTemplate);
}

}  // namespace brouk
