#include "cef/site_permissions_settings.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cef/permission_mapping.h"
#include "cef/settings_handler.h"
#include "cef/site_info.h"
#include "core/profile_id.h"
#include "core/site_permission_names.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr int kBadRequest = 400;
constexpr int kFailed = 500;
constexpr std::size_t kMaxOriginBytes = 2048;
// Sites listed at most; a profile with more is unusual.
constexpr std::size_t kMaxSites = 5000;

using Callback = CefMessageRouterBrowserSide::Callback;

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::optional<std::string> Field(const CefRefPtr<CefDictionaryValue>& message,
                                 const char* key,
                                 std::size_t max_bytes) {
  if (message->GetType(key) != VTYPE_STRING) {
    return std::nullopt;
  }
  std::string value = message->GetString(key).ToString();
  if (value.size() > max_bytes) {
    return std::nullopt;
  }
  return value;
}

// The profile's request context, if the message names a known profile.
CefRefPtr<CefRequestContext> Context(
    const CefRefPtr<CefDictionaryValue>& message,
    SettingsPageDelegate& delegate) {
  std::optional<std::string> profile = Field(message, "profile", 64);
  if (!profile || !IsValidProfileId(*profile) ||
      !delegate.Registry().Find(*profile)) {
    return nullptr;
  }
  return delegate.ProfileRequestContext(*profile);
}

// An http(s) origin as the page lists it ("https://example.com").
std::optional<std::string> OriginField(
    const CefRefPtr<CefDictionaryValue>& message) {
  std::optional<std::string> origin = Field(message, "origin", kMaxOriginBytes);
  if (!origin ||
      !(origin->starts_with("https://") || origin->starts_with("http://")) ||
      OriginOf(*origin) != *origin) {
    return std::nullopt;
  }
  return origin;
}

// Sites with a stored decision for any kind in settings, by origin.
std::set<std::string> StoredSites(const CefRefPtr<CefRequestContext>& context) {
  std::set<std::string> sites;
  for (PermissionKind kind : SiteSettingKinds()) {
    CefRefPtr<CefValue> value =
        context->GetPreference("profile.content_settings.exceptions." +
                               std::string(ContentSettingName(kind)));
    if (!value || value->GetType() != VTYPE_DICTIONARY) {
      continue;
    }
    CefDictionaryValue::KeyList keys;
    value->GetDictionary()->GetKeys(keys);
    for (const CefString& key : keys) {
      if (std::optional<std::string> origin =
              OriginFromSitePattern(key.ToString());
          origin && sites.size() < kMaxSites) {
        sites.insert(*origin);
      }
    }
  }
  return sites;
}

// "https://www.example.com" -> "example.com" plus the rest, so that a
// site's subdomains are listed together.
std::pair<std::string, std::string> SortKey(const std::string& origin) {
  std::string host = origin.substr(origin.find("://") + 3);
  std::string name = host;
  if (name.starts_with("www.")) {
    name = name.substr(4);
  }
  return {name, origin};
}

std::string StateJson(const CefRefPtr<CefRequestContext>& context) {
  std::map<std::pair<std::string, std::string>, std::vector<SitePermission>>
      sites;
  for (const std::string& origin : StoredSites(context)) {
    std::vector<SitePermission> permissions =
        GetSitePermissions(context, origin);
    if (!permissions.empty()) {
      sites.emplace(SortKey(origin), std::move(permissions));
    }
  }
  CefRefPtr<CefListValue> list = CefListValue::Create();
  for (const auto& [key, permissions] : sites) {
    CefRefPtr<CefDictionaryValue> site = CefDictionaryValue::Create();
    site->SetString("origin", key.second);
    CefRefPtr<CefListValue> items = CefListValue::Create();
    for (const SitePermission& permission : permissions) {
      CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
      item->SetString("id", std::string(ContentSettingName(permission.kind)));
      item->SetString("setting", permission.setting == PermissionSetting::kAllow
                                     ? "allow"
                                     : "block");
      items->SetDictionary(items->GetSize(), item);
    }
    site->SetList("permissions", items);
    list->SetDictionary(list->GetSize(), site);
  }
  CefRefPtr<CefListValue> kinds = CefListValue::Create();
  for (PermissionKind kind : SiteSettingKinds()) {
    CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
    item->SetString("id", std::string(ContentSettingName(kind)));
    item->SetString("name", PermissionName(kind));
    kinds->SetDictionary(kinds->GetSize(), item);
  }
  CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
  answer->SetList("sites", list);
  answer->SetList("kinds", kinds);
  return ToJson(answer);
}

}  // namespace

bool HandleSitePermissionsSettingsMessage(
    const std::string& type,
    const CefRefPtr<CefDictionaryValue>& message,
    SettingsPageDelegate& delegate,
    const CefRefPtr<Callback>& callback) {
  CEF_REQUIRE_UI_THREAD();
  if (type != "permissions.state" && type != "permissions.set" &&
      type != "permissions.reset") {
    return false;
  }
  CefRefPtr<CefRequestContext> context = Context(message, delegate);
  if (!context) {
    callback->Failure(kFailed, "The profile is not available.");
    return true;
  }
  if (type == "permissions.state") {
    callback->Success(StateJson(context));
    return true;
  }
  std::optional<std::string> origin = OriginField(message);
  if (!origin) {
    callback->Failure(kBadRequest, "Invalid request.");
    return true;
  }
  if (type == "permissions.reset") {
    ResetSitePermissions(context, *origin);
    callback->Success("");
    return true;
  }
  std::optional<std::string> id = Field(message, "id", 64);
  std::optional<PermissionKind> kind =
      id ? PermissionKindFromContentSettingName(*id) : std::nullopt;
  std::optional<std::string> setting = Field(message, "setting", 16);
  bool shown = kind && std::ranges::find(SiteSettingKinds(), *kind) !=
                           SiteSettingKinds().end();
  if (!shown || !setting ||
      (*setting != "allow" && *setting != "block" && *setting != "ask")) {
    callback->Failure(kBadRequest, "Invalid request.");
    return true;
  }
  SetSitePermission(context, *origin, *kind,
                    *setting == "allow"   ? PermissionSetting::kAllow
                    : *setting == "block" ? PermissionSetting::kBlock
                                          : PermissionSetting::kAsk);
  callback->Success("");
  return true;
}

}  // namespace brouk
