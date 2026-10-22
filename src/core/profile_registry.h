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

  // A profile's settings as stored (for export), or nullopt if unknown.
  std::optional<Settings> ProfileSettings(std::string_view id) const;
  // Creates a profile from imported settings, with a new id. If its name is
  // taken, " 2", " 3", ... is added.
  Result<ProfileId> ImportProfile(Settings settings);
  std::optional<Error> SetShowManagerAtStartup(bool show);
  bool show_manager_at_startup() const {
    return global_.GetBool("profiles.show_manager_at_startup");
  }

  // Deletes a profile's own files (profiles/<id>/). The default profile
  // cannot be deleted. Chromium may still use the profile's web data while
  // its tabs close, so that is removed at the next start, see below.
  std::optional<Error> DeleteProfile(std::string_view id);

  // Removes web data folders (<web_root>/<id>) of profiles that no longer
  // exist. Only folders named like our generated ids (p-xxxxxxxx) are
  // touched; Chromium's own files in the same folder are left alone. Call
  // after Load(), before CEF uses the folder. Returns how many were removed.
  int RemoveWebDataOfDeletedProfiles(const std::filesystem::path& web_root);

  // A colour not used by any profile yet (the first one if all are used).
  std::string UnusedColor() const;

 private:
  std::filesystem::path ProfileSettingsPath(std::string_view id) const;
  std::optional<Error> CheckName(std::string_view name,
                                 std::string_view except_id) const;
  std::optional<Error> SetGlobalSetting(std::string_view key,
                                        SettingValue value);
  void Sort();

  std::filesystem::path data_dir_;
  Settings global_;
  std::vector<ProfileInfo> profiles_;
  std::vector<std::string> problems_;
};

}  // namespace brouk

#endif  // BROUK_CORE_PROFILE_REGISTRY_H_
