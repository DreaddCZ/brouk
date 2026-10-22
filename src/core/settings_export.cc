#include "core/settings_export.h"

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "core/yaml_io.h"

namespace brouk {

namespace {

constexpr std::string_view kFormat = "brouk-settings";
constexpr std::string_view kHeader =
    "# Brouk settings (for all profiles). Import them in Brouk: Settings >\n"
    "# Import Settings.\n";

}  // namespace

std::string ExportSettings(const Settings& global,
                           std::string_view exported_at) {
  YamlNode root = YamlNode::Map();
  root.GetOrAdd("format") = YamlNode::Scalar(std::string(kFormat), true);
  root.GetOrAdd("version") =
      YamlNode::Scalar(std::to_string(kSettingsExportVersion), true);
  root.GetOrAdd("exported_at") =
      YamlNode::Scalar(std::string(exported_at), false);
  root.GetOrAdd("settings") = global.ToYaml();
  return std::string(kHeader) + EmitYaml(root);
}

Result<ImportedSettings> ParseSettingsExport(std::string_view text) {
  constexpr std::string_view kNotAnExport =
      "This file is not a Brouk settings export.";
  Result<YamlNode> parsed = ParseYaml(text);
  if (!parsed) {
    return Error{std::string(kNotAnExport)};
  }
  const YamlNode& root = parsed.value();
  const YamlNode* format =
      root.kind == YamlNode::Kind::kMap ? root.Find("format") : nullptr;
  if (!format || format->kind != YamlNode::Kind::kScalar ||
      format->scalar != kFormat) {
    return Error{std::string(kNotAnExport)};
  }
  const YamlNode* version = root.Find("version");
  int number = 0;
  if (!version || version->kind != YamlNode::Kind::kScalar ||
      std::from_chars(version->scalar.data(),
                      version->scalar.data() + version->scalar.size(), number)
              .ec != std::errc() ||
      number < 1) {
    return Error{"The file's version is missing or invalid."};
  }
  if (number > kSettingsExportVersion) {
    return Error{
        "These settings were exported by a newer version of Brouk. Update "
        "Brouk to import them."};
  }
  ImportedSettings imported{Settings(SettingScope::kGlobal), {}};
  const YamlNode* settings = root.Find("settings");
  if (!settings) {
    return Error{"The file contains no settings."};
  }
  std::vector<SettingProblem> problems;
  imported.settings =
      Settings::FromYaml(SettingScope::kGlobal, *settings, problems);
  for (const SettingProblem& problem : problems) {
    imported.problems.push_back(problem.ToString());
  }
  return imported;
}

}  // namespace brouk
