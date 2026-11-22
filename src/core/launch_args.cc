#include "core/launch_args.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brouk {

LaunchRequest ParseLaunchRequest(std::vector<std::string> arguments,
                                 std::optional<std::string> profile_switch) {
  LaunchRequest request;
  if (profile_switch && profile_switch->empty() && !arguments.empty()) {
    profile_switch = std::move(arguments.front());
    arguments.erase(arguments.begin());
  }
  if (profile_switch && !profile_switch->empty()) {
    request.profile = std::move(profile_switch);
  }
  for (std::string& url : arguments) {
    // Windows' --single-argument (024) can hand over the rest of the
    // command line with its terminating NUL: nothing after one is the URL.
    url.resize(std::min(url.size(), url.find('\0')));
    if (!url.empty()) {
      request.urls.push_back(std::move(url));
    }
  }
  return request;
}

std::string WithDisabledFeature(std::string_view list,
                                std::string_view feature) {
  std::string result;
  std::string_view rest = list;
  while (!rest.empty()) {
    std::size_t comma = rest.find(',');
    std::string_view item = rest.substr(0, comma);
    rest = comma == std::string_view::npos ? std::string_view()
                                           : rest.substr(comma + 1);
    if (item.empty() || item == feature) {
      continue;
    }
    result.append(item).append(",");
  }
  result.append(feature);
  return result;
}

}  // namespace brouk
