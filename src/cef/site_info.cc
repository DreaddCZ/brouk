#include "cef/site_info.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cef/permission_mapping.h"
#include "include/cef_navigation_entry.h"
#include "include/cef_request_context.h"
#include "include/cef_ssl_info.h"
#include "include/cef_ssl_status.h"
#include "include/cef_x509_certificate.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

std::vector<std::uint8_t> ToBytes(const CefRefPtr<CefBinaryValue>& value) {
  std::vector<std::uint8_t> bytes;
  if (value && value->GetSize() > 0) {
    bytes.resize(value->GetSize());
    value->GetData(bytes.data(), bytes.size(), 0);
  }
  return bytes;
}

}  // namespace

SiteInfo GetSiteInfo(const CefRefPtr<CefBrowser>& browser) {
  CEF_REQUIRE_UI_THREAD();
  SiteInfo info;
  CefRefPtr<CefNavigationEntry> entry =
      browser->GetHost()->GetVisibleNavigationEntry();
  if (!entry) {
    return info;
  }
  std::string url = entry->GetURL().ToString();
  info.origin = OriginOf(url);
  ConnectionFacts facts;
  facts.url = url;
  CefRefPtr<CefSSLStatus> ssl = entry->GetSSLStatus();
  if (ssl) {
    facts.is_secure_connection = ssl->IsSecureConnection();
    facts.has_certificate_error = CefIsCertStatusError(ssl->GetCertStatus());
    facts.has_insecure_content =
        (ssl->GetContentStatus() & (SSL_CONTENT_DISPLAYED_INSECURE_CONTENT |
                                    SSL_CONTENT_RAN_INSECURE_CONTENT)) != 0;
    if (CefRefPtr<CefX509Certificate> cert = ssl->GetX509Certificate()) {
      info.certificate_chain.push_back(ToBytes(cert->GetDEREncoded()));
      CefX509Certificate::IssuerChainBinaryList issuers;
      cert->GetDEREncodedIssuerChain(issuers);
      for (const CefRefPtr<CefBinaryValue>& issuer : issuers) {
        info.certificate_chain.push_back(ToBytes(issuer));
      }
    }
  }
  info.level = SecurityLevelFor(facts);
  return info;
}

std::vector<SitePermission> GetSitePermissions(
    const CefRefPtr<CefBrowser>& browser,
    const std::string& origin) {
  CEF_REQUIRE_UI_THREAD();
  std::vector<SitePermission> result;
  if (origin.empty()) {
    return result;
  }
  CefRefPtr<CefRequestContext> context =
      browser->GetHost()->GetRequestContext();
  for (PermissionKind kind : SiteSettingKinds()) {
    std::optional<cef_content_setting_types_t> type = ContentSettingFor(kind);
    if (!type) {
      continue;
    }
    cef_content_setting_values_t value =
        context->GetContentSetting(origin, origin, *type);
    if (value == CEF_CONTENT_SETTING_VALUE_ALLOW) {
      result.push_back({kind, PermissionSetting::kAllow});
    } else if (value == CEF_CONTENT_SETTING_VALUE_BLOCK) {
      result.push_back({kind, PermissionSetting::kBlock});
    }
  }
  return result;
}

void SetSitePermission(const CefRefPtr<CefBrowser>& browser,
                       const std::string& origin,
                       PermissionKind kind,
                       PermissionSetting setting) {
  CEF_REQUIRE_UI_THREAD();
  std::optional<cef_content_setting_types_t> type = ContentSettingFor(kind);
  if (!type || origin.empty()) {
    return;
  }
  cef_content_setting_values_t value = CEF_CONTENT_SETTING_VALUE_DEFAULT;
  if (setting == PermissionSetting::kAllow) {
    value = CEF_CONTENT_SETTING_VALUE_ALLOW;
  } else if (setting == PermissionSetting::kBlock) {
    value = CEF_CONTENT_SETTING_VALUE_BLOCK;
  }
  browser->GetHost()->GetRequestContext()->SetContentSetting(origin, origin,
                                                             *type, value);
}

void ResetSitePermissions(const CefRefPtr<CefBrowser>& browser,
                          const std::string& origin) {
  for (PermissionKind kind : SiteSettingKinds()) {
    SetSitePermission(browser, origin, kind, PermissionSetting::kAsk);
  }
}

}  // namespace brouk
