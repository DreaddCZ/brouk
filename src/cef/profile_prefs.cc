#include "cef/profile_prefs.h"

#include <cstddef>
#include <string>

#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

CefString ExternalProtocolAllowListPref() {
  return "brouk.external_protocol_allow_list";
}

}  // namespace

void RegisterProfilePreferences(cef_preferences_type_t type,
                                CefRawPtr<CefPreferenceRegistrar> registrar) {
  if (type != CEF_PREFERENCES_TYPE_REQUEST_CONTEXT) {
    return;
  }
  CefRefPtr<CefValue> empty_list = CefValue::Create();
  empty_list->SetList(CefListValue::Create());
  registrar->AddPreference(ExternalProtocolAllowListPref(), empty_list);
}

bool IsExternalProtocolAllowed(const CefRefPtr<CefRequestContext>& context,
                               const std::string& key) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefValue> value =
      context->GetPreference(ExternalProtocolAllowListPref());
  if (!value || value->GetType() != VTYPE_LIST) {
    return false;
  }
  CefRefPtr<CefListValue> list = value->GetList();
  for (std::size_t i = 0; i < list->GetSize(); ++i) {
    if (list->GetType(i) == VTYPE_STRING && list->GetString(i) == key) {
      return true;
    }
  }
  return false;
}

void AllowExternalProtocol(const CefRefPtr<CefRequestContext>& context,
                           const std::string& key) {
  CEF_REQUIRE_UI_THREAD();
  if (IsExternalProtocolAllowed(context, key)) {
    return;
  }
  CefRefPtr<CefValue> value =
      context->GetPreference(ExternalProtocolAllowListPref());
  CefRefPtr<CefListValue> list = value && value->GetType() == VTYPE_LIST
                                     ? value->GetList()->Copy()
                                     : CefListValue::Create();
  list->SetString(list->GetSize(), key);
  CefRefPtr<CefValue> updated = CefValue::Create();
  updated->SetList(list);
  CefString error;
  context->SetPreference(ExternalProtocolAllowListPref(), updated, error);
}

}  // namespace brouk
