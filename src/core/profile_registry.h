#ifndef BROUK_CORE_PROFILE_REGISTRY_H_
#define BROUK_CORE_PROFILE_REGISTRY_H_

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/profile_id.h"
#include "core/result.h"
#include "core/settings.h"

namespace brouk {

// The first profile's name on first start.
inline constexpr std::string_view kFirstProfileName = "Main";

struct ProfileInfo {
  ProfileId id;
  std::string name;
  std::string color;  // One of ProfileColorNames().
};

// The profiles and the global settings, as stored in the data folder:
//   <data>/settings.yaml               global settings
//   <data>/profiles/<id>/settings.yaml  one per profile
// Everything here is read from files the user may edit, so it is validated
// and problems are collected for the user instead of failing.
class ProfileRegistry {
 public:
  explicit ProfileRegistry(std::filesystem::path data_dir);

  // (Re)reads everything. Creates the first profile if there is none.
  void Load();

  // Sorted by name (then id), as menus show them.
  const std::vector<ProfileInfo>& profiles() const { return profiles_; }
  const ProfileInfo* Find(std::string_view id) const;
  // Case-insensitive (ASCII) name match, for --profile on the command line.
  const ProfileInfo* FindByName(std::string_view name) const;
  // The default profile; the first profile if the setting names none that
  // exists.
  const ProfileInfo& default_profile() const;
  const Settings& global_settings() const { return global_; }
  // Problems found by the last Load(), e.g.
  // "profiles/p-1a2b/settings.yaml: line 2, profile.color: must be one of …".
  const std::vector<std::string>& problems() const { return problems_; }

  // Creates a profile (folder and settings file) and returns its id. Names
  // must be non-empty and unique (ignoring case).
  Result<ProfileId> CreateProfile(std::string_view name,
                                  std::string_view color);
  std::optional<Error> UpdateProfile(std::string_view id,
                                     std::string_view name,
                                     std::string_view color);
  std::optional<Error> SetDefaultProfile(std::string_view id);

  // A colour not used by any profile yet (the first one if all are used).
  std::string UnusedColor() const;

 private:
  std::filesystem::path ProfileSettingsPath(std::string_view id) const;
  std::optional<Error> CheckName(std::string_view name,
                                 std::string_view except_id) const;
  void Sort();

  std::filesystem::path data_dir_;
  Settings global_;
  std::vector<ProfileInfo> profiles_;
  std::vector<std::string> problems_;
};

}  // namespace brouk

#endif  // BROUK_CORE_PROFILE_REGISTRY_H_
