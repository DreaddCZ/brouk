#include "core/settings_file.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "core/yaml_io.h"

namespace brouk {

namespace {

constexpr std::string_view kHeader =
    "# Brouk settings. You can edit this file while Brouk runs; changes apply\n"
    "# at once. Brouk rewrites it when settings change in the app, so\n"
    "# comments are not kept.\n";

}  // namespace

LoadedSettings LoadSettingsFile(const std::filesystem::path& path,
                                SettingScope scope) {
  LoadedSettings loaded{Settings(scope), {}};
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    return loaded;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    loaded.problems.push_back({{}, 0, "the file cannot be opened"});
    return loaded;
  }
  std::string text((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  if (file.bad()) {
    loaded.problems.push_back({{}, 0, "the file cannot be read"});
    return loaded;
  }
  Result<YamlNode> root = ParseYaml(text);
  if (!root) {
    loaded.problems.push_back({{}, 0, root.error().message});
    return loaded;
  }
  loaded.settings = Settings::FromYaml(scope, root.value(), loaded.problems);
  return loaded;
}

std::optional<Error> SaveSettingsFile(const std::filesystem::path& path,
                                      const Settings& settings) {
  std::string yaml = EmitYaml(settings.ToYaml());
  if (yaml.empty()) {
    return Error{"the settings could not be written as YAML"};
  }
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return Error{"cannot create " + path.parent_path().string() + ": " +
                 error.message()};
  }
  // Write a temporary file next to it, then rename: readers (and a crash)
  // never see a half-written file.
  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file << kHeader << yaml;
    file.close();
    if (!file) {
      std::filesystem::remove(temp, error);
      return Error{"cannot write " + temp.string()};
    }
  }
  std::filesystem::permissions(
      temp,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      error);
  std::filesystem::rename(temp, path, error);
  if (error) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return Error{"cannot replace " + path.string() + ": " + error.message()};
  }
  return std::nullopt;
}

}  // namespace brouk
