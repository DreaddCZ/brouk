#include "cef/profile_manager_handler.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "cef/json_util.h"
#include "core/internal_pages.h"
#include "core/profile_id.h"
#include "core/settings_schema.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxRequestBytes = std::size_t{16} * 1024;
constexpr std::size_t kMaxFieldBytes = 1024;
// Error codes for the page (it shows the message).
constexpr int kBadRequest = 400;
constexpr int kFailed = 500;

std::optional<std::string> StringField(const CefRefPtr<CefDictionaryValue>& d,
                                       const char* key) {
  if (d->GetType(key) != VTYPE_STRING) {
    return std::nullopt;
  }
  std::string value = d->GetString(key).ToString();
  if (value.size() > kMaxFieldBytes) {
    return std::nullopt;
  }
  return value;
}

std::optional<ProfileId> IdField(const CefRefPtr<CefDictionaryValue>& d) {
  std::optional<std::string> id = StringField(d, "id");
  if (!id || !IsValidProfileId(*id)) {
    return std::nullopt;
  }
  return id;
}

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::string StateJson(ProfileManagerDelegate& delegate) {
  CefRefPtr<CefListValue> profiles = CefListValue::Create();
  std::size_t index = 0;
  for (const ProfileManagerDelegate::Summary& summary : delegate.Profiles()) {
    CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
    item->SetString("id", summary.info.id);
    item->SetString("name", summary.info.name);
    item->SetString("color", summary.info.color);
    item->SetBool("isDefault", summary.is_default);
    item->SetInt("openTabs", summary.open_tabs);
    profiles->SetDictionary(index++, item);
  }
  CefRefPtr<CefListValue> colors = CefListValue::Create();
  index = 0;
  for (std::string_view color : ProfileColorNames()) {
    colors->SetString(index++, std::string(color));
  }
  CefRefPtr<CefDictionaryValue> state = CefDictionaryValue::Create();
  state->SetList("profiles", profiles);
  state->SetList("colors", colors);
  state->SetBool("showAtStartup", delegate.ShowAtStartup());
  return ToJson(state);
}

void Answer(const CefRefPtr<CefMessageRouterBrowserSide::Callback>& callback,
            const std::optional<Error>& error) {
  if (error) {
    callback->Failure(kFailed, error->message);
  } else {
    callback->Success("");
  }
}

}  // namespace

bool ProfileManagerHandler::OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                                    CefRefPtr<CefFrame> frame,
                                    int64_t /*query_id*/,
                                    const CefString& request,
                                    bool persistent,
                                    CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  // Only the profile manager page itself; the URL is the browser's own view
  // of the frame, not something the page can claim.
  if (!frame || InternalPageOf(frame->GetURL().ToString()) != "profiles") {
    return false;
  }
  std::string text = request.ToString();
  CefRefPtr<CefValue> parsed = text.size() <= kMaxRequestBytes && !persistent
                                   ? ParseJson(text)
                                   : nullptr;
  CefRefPtr<CefDictionaryValue> message =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  std::optional<std::string> type =
      message ? StringField(message, "type") : std::nullopt;
  if (!type) {
    callback->Failure(kBadRequest, "Invalid request.");
    return true;
  }
  ProfileManagerDelegate& delegate = *delegate_;

  if (*type == "profiles.state") {
    callback->Success(StateJson(delegate));
  } else if (*type == "profiles.create") {
    std::optional<std::string> name = StringField(message, "name");
    std::optional<std::string> color = StringField(message, "color");
    if (!name || !color) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    Result<ProfileId> created = delegate.CreateProfile(*name, *color);
    if (!created) {
      callback->Failure(kFailed, created.error().message);
      return true;
    }
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetString("id", created.value());
    callback->Success(ToJson(answer));
  } else if (*type == "profiles.update") {
    std::optional<ProfileId> id = IdField(message);
    std::optional<std::string> name = StringField(message, "name");
    std::optional<std::string> color = StringField(message, "color");
    if (!id || !name || !color) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    Answer(callback, delegate.UpdateProfile(*id, *name, *color));
  } else if (*type == "profiles.setDefault" || *type == "profiles.open" ||
             *type == "profiles.delete") {
    std::optional<ProfileId> id = IdField(message);
    if (!id) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    if (*type == "profiles.setDefault") {
      Answer(callback, delegate.SetDefaultProfile(*id));
    } else if (*type == "profiles.open") {
      Answer(callback, delegate.OpenProfile(*id));
    } else {
      delegate.DeleteProfile(
          *id, [callback](bool deleted, std::optional<Error> error) {
            if (error) {
              callback->Failure(kFailed, error->message);
              return;
            }
            CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
            answer->SetBool("deleted", deleted);
            callback->Success(ToJson(answer));
          });
    }
  } else if (*type == "profiles.export") {
    std::optional<ProfileId> id = IdField(message);
    if (!id) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    delegate.ExportProfile(
        *id, [callback](bool saved, std::optional<Error> error) {
          if (error) {
            callback->Failure(kFailed, error->message);
            return;
          }
          CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
          answer->SetBool("saved", saved);
          callback->Success(ToJson(answer));
        });
  } else if (*type == "profiles.import") {
    delegate.ImportProfile(
        [callback](Result<ProfileManagerDelegate::ImportResult> result) {
          if (!result) {
            callback->Failure(kFailed, result.error().message);
            return;
          }
          CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
          if (result.value().id) {
            answer->SetString("id", *result.value().id);
          } else {
            answer->SetNull("id");
          }
          CefRefPtr<CefListValue> problems = CefListValue::Create();
          std::size_t index = 0;
          for (const std::string& problem : result.value().problems) {
            problems->SetString(index++, problem);
          }
          answer->SetList("problems", problems);
          callback->Success(ToJson(answer));
        });
  } else if (*type == "profiles.setShowAtStartup") {
    if (message->GetType("show") != VTYPE_BOOL) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    Answer(callback, delegate.SetShowAtStartup(message->GetBool("show")));
  } else {
    callback->Failure(kBadRequest, "Unknown request.");
  }
  return true;
}

}  // namespace brouk
