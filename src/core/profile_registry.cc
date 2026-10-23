#include "core/profile_registry.h"

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/settings_file.h"
#include "core/settings_schema.h"

namespace brouk {

namespace {

constexpr std::string_view kSettingsFile = "settings.yaml";
constexpr std::string_view kProfilesDir = "profiles";

char ToLowerAscii(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool EqualsIgnoringCase(std::string_view a, std::string_view b) {
  return std::ranges::equal(
      a, b, [](char x, char y) { return ToLowerAscii(x) == ToLowerAscii(y); });
}

// "p-" and 8 lower-case hex digits, as NewProfileId() makes them.
bool IsGeneratedProfileId(std::string_view id) {
  return id.size() == 10 && id.starts_with("p-") &&
         id.substr(2).find_first_not_of("0123456789abcdef") ==
             std::string_view::npos;
}

std::string NewProfileId() {
  std::random_device random;
  std::uniform_int_distribution<int> hex(0, 15);
  std::string id = "p-";
  for (int i = 0; i < 8; ++i) {
    id += "0123456789abcdef"[hex(random)];
  }
  return id;
}

ProfileInfo InfoFrom(std::string id, const Settings& settings) {
  return {std::move(id), settings.GetString("profile.name"),
          settings.GetString("profile.color")};
}

}  // namespace

bool IsValidProfileId(std::string_view id) {
  return !id.empty() && id.size() <= 32 && std::ranges::all_of(id, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
  });
}

ProfileRegistry::ProfileRegistry(std::filesystem::path data_dir)
    : data_dir_(std::move(data_dir)), global_(SettingScope::kGlobal) {}

void ProfileRegistry::Load() {
  problems_.clear();
  profiles_.clear();

  LoadedSettings global =
      LoadSettingsFile(data_dir_ / kSettingsFile, SettingScope::kGlobal);
  global_ = global.settings;
  for (const SettingProblem& problem : global.problems) {
    problems_.push_back(std::string(kSettingsFile) + ": " + problem.ToString());
  }

  std::error_code error;
  std::filesystem::path profiles_dir = data_dir_ / kProfilesDir;
  for (std::filesystem::directory_iterator it(profiles_dir, error), end;
       !error && it != end; it.increment(error)) {
    if (!it->is_directory(error)) {
      continue;
    }
    std::string id = it->path().filename().string();
    if (!IsValidProfileId(id)) {
      problems_.push_back(std::string(kProfilesDir) + "/" + id +
                          ": not a profile folder name (ignored)");
      continue;
    }
    std::filesystem::path path = ProfileSettingsPath(id);
    bool has_file = std::filesystem::exists(path, error);
    LoadedSettings loaded = LoadSettingsFile(path, SettingScope::kProfile);
    if (!has_file && id == kFirstProfileId) {
      // A folder from before profiles existed: give it its name.
      loaded.settings.Set("profile.name", std::string(kFirstProfileName));
    }
    for (const SettingProblem& problem : loaded.problems) {
      problems_.push_back(std::string(kProfilesDir) + "/" + id + "/" +
                          std::string(kSettingsFile) + ": " +
                          problem.ToString());
    }
    if (!has_file) {
      SaveSettingsFile(path, loaded.settings);
    }
    profiles_.push_back(InfoFrom(id, loaded.settings));
  }

  if (profiles_.empty()) {
    Settings first(SettingScope::kProfile);
    first.Set("profile.name", std::string(kFirstProfileName));
    if (std::optional<Error> save =
            SaveSettingsFile(ProfileSettingsPath(kFirstProfileId), first)) {
      problems_.push_back(save->message);
    }
    profiles_.push_back(InfoFrom(std::string(kFirstProfileId), first));
  }
  Sort();
}

const ProfileInfo* ProfileRegistry::Find(std::string_view id) const {
  auto it = std::ranges::find(profiles_, id, &ProfileInfo::id);
  return it == profiles_.end() ? nullptr : &*it;
}

const ProfileInfo* ProfileRegistry::FindByName(std::string_view name) const {
  auto it = std::ranges::find_if(profiles_, [name](const ProfileInfo& p) {
    return EqualsIgnoringCase(p.name, name);
  });
  return it == profiles_.end() ? nullptr : &*it;
}

const ProfileInfo& ProfileRegistry::default_profile() const {
  // Load() always leaves at least one profile.
  const ProfileInfo* configured = Find(global_.GetString("profiles.default"));
  if (configured) {
    return *configured;
  }
  const ProfileInfo* first = Find(kFirstProfileId);
  return first ? *first : profiles_.front();
}

Result<ProfileId> ProfileRegistry::CreateProfile(std::string_view name,
                                                 std::string_view color) {
  if (std::optional<Error> problem = CheckName(name, {})) {
    return *problem;
  }
  Settings settings(SettingScope::kProfile);
  if (std::optional<std::string> problem =
          settings.Set("profile.name", std::string(name))) {
    return Error{"Name " + *problem};
  }
  if (std::optional<std::string> problem =
          settings.Set("profile.color", std::string(color))) {
    return Error{"Colour " + *problem};
  }
  std::string id = NewProfileId();
  std::error_code error;
  while (Find(id) ||
         std::filesystem::exists(data_dir_ / kProfilesDir / id, error)) {
    id = NewProfileId();
  }
  if (std::optional<Error> save =
          SaveSettingsFile(ProfileSettingsPath(id), settings)) {
    return *save;
  }
  profiles_.push_back(InfoFrom(id, settings));
  Sort();
  return id;
}

std::optional<Settings> ProfileRegistry::ProfileSettings(
    std::string_view id) const {
  if (!Find(id)) {
    return std::nullopt;
  }
  return LoadSettingsFile(ProfileSettingsPath(id), SettingScope::kProfile)
      .settings;
}

Result<ProfileId> ProfileRegistry::ImportProfile(Settings settings) {
  std::string base = settings.GetString("profile.name");
  if (base.find_first_not_of(" \t") == std::string::npos) {
    base = "Imported profile";
  }
  std::string name = base;
  for (int n = 2; CheckName(name, {}); ++n) {
    std::string suffix = " " + std::to_string(n);
    // Keep within the name's length limit.
    name = base.substr(0, 64 - suffix.size()) + suffix;
  }
  if (std::optional<std::string> problem = settings.Set("profile.name", name)) {
    return Error{"Name " + *problem};
  }
  std::string id = NewProfileId();
  std::error_code error;
  while (Find(id) ||
         std::filesystem::exists(data_dir_ / kProfilesDir / id, error)) {
    id = NewProfileId();
  }
  if (std::optional<Error> save =
          SaveSettingsFile(ProfileSettingsPath(id), settings)) {
    return *save;
  }
  profiles_.push_back(InfoFrom(id, settings));
  Sort();
  return id;
}

std::optional<Error> ProfileRegistry::UpdateProfile(std::string_view id,
                                                    std::string_view name,
                                                    std::string_view color) {
  auto it = std::ranges::find(profiles_, id, &ProfileInfo::id);
  if (it == profiles_.end()) {
    return Error{"There is no such profile."};
  }
  if (std::optional<Error> problem = CheckName(name, id)) {
    return problem;
  }
  // Keep the other values of the file as they are.
  LoadedSettings loaded =
      LoadSettingsFile(ProfileSettingsPath(id), SettingScope::kProfile);
  if (std::optional<std::string> problem =
          loaded.settings.Set("profile.name", std::string(name))) {
    return Error{"Name " + *problem};
  }
  if (std::optional<std::string> problem =
          loaded.settings.Set("profile.color", std::string(color))) {
    return Error{"Colour " + *problem};
  }
  if (std::optional<Error> save =
          SaveSettingsFile(ProfileSettingsPath(id), loaded.settings)) {
    return save;
  }
  *it = InfoFrom(std::string(id), loaded.settings);
  Sort();
  return std::nullopt;
}

std::optional<Error> ProfileRegistry::SetDefaultProfile(std::string_view id) {
  if (!Find(id)) {
    return Error{"There is no such profile."};
  }
  return SetGlobalSetting("profiles.default", std::string(id));
}

std::optional<Error> ProfileRegistry::SetShowManagerAtStartup(bool show) {
  return SetGlobalSetting("profiles.show_manager_at_startup", show);
}

std::optional<Error> ProfileRegistry::SetGlobalSetting(std::string_view key,
                                                       SettingValue value) {
  // Keep the other values of the file as they are.
  LoadedSettings loaded =
      LoadSettingsFile(data_dir_ / kSettingsFile, SettingScope::kGlobal);
  if (std::optional<std::string> problem =
          loaded.settings.Set(key, std::move(value))) {
    return Error{*problem};
  }
  if (std::optional<Error> save =
          SaveSettingsFile(data_dir_ / kSettingsFile, loaded.settings)) {
    return save;
  }
  global_ = loaded.settings;
  return std::nullopt;
}

std::optional<Error> ProfileRegistry::SetProfileSetting(std::string_view id,
                                                        std::string_view key,
                                                        SettingValue value) {
  auto it = std::ranges::find(profiles_, id, &ProfileInfo::id);
  if (it == profiles_.end()) {
    return Error{"There is no such profile."};
  }
  if (key == "profile.name") {
    const std::string* name = std::get_if<std::string>(&value);
    if (name) {
      if (std::optional<Error> problem = CheckName(*name, id)) {
        return problem;
      }
    }
  }
  LoadedSettings loaded =
      LoadSettingsFile(ProfileSettingsPath(id), SettingScope::kProfile);
  if (std::optional<std::string> problem =
          loaded.settings.Set(key, std::move(value))) {
    return Error{*problem};
  }
  if (std::optional<Error> save =
          SaveSettingsFile(ProfileSettingsPath(id), loaded.settings)) {
    return save;
  }
  *it = InfoFrom(std::string(id), loaded.settings);
  Sort();
  return std::nullopt;
}

std::optional<Error> ProfileRegistry::ReplaceGlobalSettings(
    const Settings& settings) {
  if (settings.scope() != SettingScope::kGlobal) {
    return Error{"These are not global settings."};
  }
  if (std::optional<Error> save =
          SaveSettingsFile(data_dir_ / kSettingsFile, settings)) {
    return save;
  }
  global_ = settings;
  return std::nullopt;
}

std::optional<Error> ProfileRegistry::DeleteProfile(std::string_view id) {
  auto it = std::ranges::find(profiles_, id, &ProfileInfo::id);
  if (it == profiles_.end()) {
    return Error{"There is no such profile."};
  }
  if (default_profile().id == id) {
    return Error{
        "The default profile cannot be deleted. Make another profile the "
        "default first."};
  }
  if (profiles_.size() == 1) {
    return Error{"The last profile cannot be deleted."};
  }
  std::error_code error;
  std::filesystem::remove_all(data_dir_ / kProfilesDir / std::string(id),
                              error);
  if (error) {
    return Error{"The profile's files could not be removed: " +
                 error.message()};
  }
  profiles_.erase(it);
  return std::nullopt;
}

int ProfileRegistry::RemoveWebDataOfDeletedProfiles(
    const std::filesystem::path& web_root) {
  int removed = 0;
  std::error_code error;
  std::vector<std::filesystem::path> orphans;
  for (std::filesystem::directory_iterator it(web_root, error), end;
       !error && it != end; it.increment(error)) {
    std::string name = it->path().filename().string();
    if (IsGeneratedProfileId(name) && !Find(name) && it->is_directory(error)) {
      orphans.push_back(it->path());
    }
  }
  for (const std::filesystem::path& path : orphans) {
    std::error_code remove_error;
    std::filesystem::remove_all(path, remove_error);
    if (!remove_error) {
      ++removed;
    }
  }
  return removed;
}

std::string ProfileRegistry::UnusedColor() const {
  for (std::string_view color : ProfileColorNames()) {
    if (std::ranges::none_of(profiles_, [color](const ProfileInfo& p) {
          return p.color == color;
        })) {
      return std::string(color);
    }
  }
  return std::string(ProfileColorNames().front());
}

std::filesystem::path ProfileRegistry::ProfileSettingsPath(
    std::string_view id) const {
  return data_dir_ / kProfilesDir / std::string(id) / kSettingsFile;
}

std::optional<Error> ProfileRegistry::CheckName(
    std::string_view name,
    std::string_view except_id) const {
  if (name.find_first_not_of(" \t") == std::string_view::npos) {
    return Error{"The name must not be empty."};
  }
  for (const ProfileInfo& profile : profiles_) {
    if (profile.id != except_id && EqualsIgnoringCase(profile.name, name)) {
      return Error{"Another profile is already called \"" + profile.name +
                   "\"."};
    }
  }
  return std::nullopt;
}

void ProfileRegistry::Sort() {
  std::ranges::sort(profiles_, [](const ProfileInfo& a, const ProfileInfo& b) {
    std::string la(a.name.size(), ' ');
    std::string lb(b.name.size(), ' ');
    std::ranges::transform(a.name, la.begin(), ToLowerAscii);
    std::ranges::transform(b.name, lb.begin(), ToLowerAscii);
    return la != lb ? la < lb : a.id < b.id;
  });
}

}  // namespace brouk
