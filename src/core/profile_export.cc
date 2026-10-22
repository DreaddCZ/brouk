#include "core/profile_export.h"

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/yaml_io.h"

namespace brouk {

namespace {

constexpr std::string_view kFormat = "brouk-profile";
constexpr std::string_view kHeader =
    "# A Brouk profile: its name, colour and settings. It contains no\n"
    "# cookies, sign-ins or cache. Import it in Brouk: Profiles > Manage\n"
    "# Profiles > Import.\n";

}  // namespace

std::string ExportProfile(const Settings& profile_settings,
                          std::string_view exported_at) {
  YamlNode root = YamlNode::Map();
  root.GetOrAdd("format") = YamlNode::Scalar(std::string(kFormat), true);
  root.GetOrAdd("version") =
      YamlNode::Scalar(std::to_string(kProfileExportVersion), true);
  root.GetOrAdd("exported_at") =
      YamlNode::Scalar(std::string(exported_at), false);
  root.GetOrAdd("settings") = profile_settings.ToYaml();
  return std::string(kHeader) + EmitYaml(root);
}

Result<ImportedProfile> ParseProfileExport(std::string_view text) {
  constexpr std::string_view kNotAnExport =
      "This file is not a Brouk profile export.";
  Result<YamlNode> parsed = ParseYaml(text);
  if (!parsed) {
    return Error{std::string(kNotAnExport) + " (" + parsed.error().message +
                 ")"};
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
  if (number > kProfileExportVersion) {
    return Error{
        "This profile was exported by a newer version of Brouk. Update "
        "Brouk to import it."};
  }

  ImportedProfile imported{Settings(SettingScope::kProfile), {}};
  const YamlNode* settings = root.Find("settings");
  if (!settings) {
    imported.problems.emplace_back("The file contains no settings.");
  } else {
    std::vector<SettingProblem> problems;
    imported.settings =
        Settings::FromYaml(SettingScope::kProfile, *settings, problems);
    for (const SettingProblem& problem : problems) {
      imported.problems.push_back("Settings: " + problem.ToString());
    }
  }
  for (const YamlEntry& entry : root.entries) {
    if (entry.key != "format" && entry.key != "version" &&
        entry.key != "exported_at" && entry.key != "settings") {
      imported.problems.push_back("\"" + entry.key +
                                  "\" is not imported by this version of "
                                  "Brouk.");
    }
  }
  return imported;
}

}  // namespace brouk
