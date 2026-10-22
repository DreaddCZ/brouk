#include "core/launch_args.h"

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
  request.urls = std::move(arguments);
  return request;
}

}  // namespace brouk
