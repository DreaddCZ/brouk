#include "core/launch_args.h"

#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

using Strings = std::vector<std::string>;

TEST(LaunchArgsTest, UrlsOnly) {
  LaunchRequest request = ParseLaunchRequest({"a.com", "b.com"}, std::nullopt);
  EXPECT_EQ(request.urls, (Strings{"a.com", "b.com"}));
  EXPECT_FALSE(request.profile);
}

TEST(LaunchArgsTest, ProfileWithEqualsSign) {
  LaunchRequest request = ParseLaunchRequest({"a.com"}, "Work");
  EXPECT_EQ(request.profile, "Work");
  EXPECT_EQ(request.urls, (Strings{"a.com"}));
}

TEST(LaunchArgsTest, ProfileAsNextArgument) {
  LaunchRequest request = ParseLaunchRequest({"Client A", "a.com"}, "");
  EXPECT_EQ(request.profile, "Client A");
  EXPECT_EQ(request.urls, (Strings{"a.com"}));
}

TEST(LaunchArgsTest, BareProfileSwitchWithoutArguments) {
  LaunchRequest request = ParseLaunchRequest({}, "");
  EXPECT_FALSE(request.profile);
  EXPECT_TRUE(request.urls.empty());
}

}  // namespace
}  // namespace brouk
