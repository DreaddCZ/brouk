#include "cef/tab_environment_commands.h"

#include <string>

#include "include/cef_version.h"

namespace brouk {

namespace {

CefRefPtr<CefDictionaryValue> Brand(std::string_view brand,
                                    const std::string& version) {
  CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
  item->SetString("brand", std::string(brand));
  item->SetString("version", version);
  return item;
}

// User agent client hints as the preset's browser sends them.
CefRefPtr<CefDictionaryValue> Metadata(const UserAgentPreset& preset) {
  const std::string major = std::to_string(CHROME_VERSION_MAJOR);
  const std::string full = major + ".0.0.0";
  CefRefPtr<CefListValue> brands = CefListValue::Create();
  CefRefPtr<CefListValue> full_list = CefListValue::Create();
  int index = 0;
  for (std::string_view brand : {std::string_view("Chromium"), preset.brand,
                                 std::string_view("Not_A Brand")}) {
    const bool grease = brand == "Not_A Brand";
    brands->SetDictionary(index, Brand(brand, grease ? "24" : major));
    full_list->SetDictionary(index, Brand(brand, grease ? "24.0.0.0" : full));
    ++index;
  }
  CefRefPtr<CefDictionaryValue> metadata = CefDictionaryValue::Create();
  metadata->SetList("brands", brands);
  metadata->SetList("fullVersionList", full_list);
  metadata->SetString("platform", std::string(preset.hint_platform));
  metadata->SetString("platformVersion",
                      std::string(preset.hint_platform_version));
  metadata->SetString("architecture", preset.mobile ? "" : "x86");
  metadata->SetString("model", preset.mobile ? "K" : "");
  metadata->SetBool("mobile", preset.mobile);
  return metadata;
}

CefRefPtr<CefDictionaryValue> MediaFeature(const std::string& name,
                                           const std::string& value) {
  CefRefPtr<CefDictionaryValue> feature = CefDictionaryValue::Create();
  feature->SetString("name", name);
  feature->SetString("value", value);
  return feature;
}

}  // namespace

std::vector<TabEnvironmentCommand> TabEnvironmentCommands(
    const TabEnvironment& env) {
  std::vector<TabEnvironmentCommand> commands;

  CefRefPtr<CefDictionaryValue> zone = CefDictionaryValue::Create();
  zone->SetString("timezoneId", env.time_zone);  // Empty: the real one.
  commands.emplace_back("Emulation.setTimezoneOverride", zone);

  // Intl and date formatting; without "locale" the override ends.
  CefRefPtr<CefDictionaryValue> locale = CefDictionaryValue::Create();
  if (!env.languages.empty()) {
    locale->SetString("locale", env.languages.front());
  }
  commands.emplace_back("Emulation.setLocaleOverride", locale);

  // User agent, navigator.platform, client hints, Accept-Language and
  // navigator.languages. An empty user agent keeps the browser's own.
  CefRefPtr<CefDictionaryValue> agent = CefDictionaryValue::Create();
  std::string user_agent = env.user_agent;
  if (const UserAgentPreset* preset =
          FindUserAgentPreset(env.user_agent_preset)) {
    user_agent = PresetUserAgent(*preset, CHROME_VERSION_MAJOR);
    agent->SetString("platform", std::string(preset->platform));
    if (!preset->brand.empty()) {
      agent->SetDictionary("userAgentMetadata", Metadata(*preset));
    }
  }
  agent->SetString("userAgent", user_agent);
  if (!env.languages.empty()) {
    // A plain list: Chromium adds the q-values to the header itself.
    std::string list;
    for (const std::string& language : env.languages) {
      list += (list.empty() ? "" : ",") + language;
    }
    agent->SetString("acceptLanguage", list);
  }
  commands.emplace_back("Emulation.setUserAgentOverride", agent);

  if (!env.location) {
    commands.emplace_back("Emulation.clearGeolocationOverride",
                          CefDictionaryValue::Create());
  } else {
    // Without coordinates: the position is unavailable.
    CefRefPtr<CefDictionaryValue> at = CefDictionaryValue::Create();
    if (!env.location->unavailable) {
      at->SetDouble("latitude", env.location->latitude);
      at->SetDouble("longitude", env.location->longitude);
      at->SetDouble("accuracy", env.location->accuracy);
    }
    commands.emplace_back("Emulation.setGeolocationOverride", at);
  }

  CefRefPtr<CefListValue> features = CefListValue::Create();
  if (!env.color_scheme.empty()) {
    features->SetDictionary(
        features->GetSize(),
        MediaFeature("prefers-color-scheme", env.color_scheme));
  }
  if (env.reduced_motion) {
    features->SetDictionary(features->GetSize(),
                            MediaFeature("prefers-reduced-motion", "reduce"));
  }
  CefRefPtr<CefDictionaryValue> media = CefDictionaryValue::Create();
  media->SetString("media", "");
  media->SetList("features", features);
  commands.emplace_back("Emulation.setEmulatedMedia", media);
  return commands;
}

std::string TabEnvironmentCommandName(std::string_view method) {
  if (method == "Emulation.setTimezoneOverride") {
    return "Time zone";
  }
  if (method == "Emulation.setLocaleOverride") {
    return "Language";
  }
  if (method == "Emulation.setUserAgentOverride") {
    return "User agent and language";
  }
  if (method == "Emulation.setEmulatedMedia") {
    return "Colour scheme";
  }
  return "Location";
}

}  // namespace brouk
