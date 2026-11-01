#include "core/launch_args.h"

#include <algorithm>
#include <optional>
#include <string>
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

}  // namespace brouk
