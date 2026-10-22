#ifndef BROUK_CORE_PROFILE_ID_H_
#define BROUK_CORE_PROFILE_ID_H_

#include <string>
#include <string_view>

namespace brouk {

// A profile's stable id (005): names its folders (profiles/<id>/, web/<id>/)
// and never changes, unlike its name.
using ProfileId = std::string;

// The profile that exists from the first start. It can be renamed, and
// another profile can become the default.
inline constexpr std::string_view kFirstProfileId = "default";

// Lower-case letters, digits and '-', 1 to 32 characters: safe as a folder
// name on every platform.
bool IsValidProfileId(std::string_view id);

}  // namespace brouk

#endif  // BROUK_CORE_PROFILE_ID_H_
