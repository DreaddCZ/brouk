#include "core/search_engines.h"

#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "core/address_input.h"

namespace brouk {

namespace {

constexpr std::array<std::pair<std::string_view, std::string_view>, 5> kNames =
    {{
        {"google", "Google"},
        {"bing", "Bing"},
        {"duckduckgo", "DuckDuckGo"},
        {"startpage", "Startpage"},
        {"ecosia", "Ecosia"},
    }};

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

std::string SearchEngineName(const Settings& global) {
  std::string url = SearchTemplate(global);
  for (const auto& [engine, name] : kNames) {
    for (const auto& [engine2, template_url] : kEngines) {
      if (engine == engine2 && template_url == url) {
        return std::string(name);
      }
    }
  }
  // A custom engine: its host, e.g. "kagi.com".
  std::string_view host = url;
  host.remove_prefix(host.find("://") + 3);
  host = host.substr(0, host.find_first_of("/?#:"));
  if (host.starts_with("www.")) {
    host.remove_prefix(4);
  }
  return std::string(host);
}

}  // namespace brouk
