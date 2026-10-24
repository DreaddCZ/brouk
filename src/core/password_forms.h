#ifndef BROUK_CORE_PASSWORD_FORMS_H_
#define BROUK_CORE_PASSWORD_FORMS_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/password_store.h"

namespace brouk {

// Login forms as the page script (web/forms) reports them, and the
// decisions the password manager (008) takes about them. Everything from the
// page is untrusted: parsed with limits here, and the page never learns
// more than the one entry the browser chooses to fill.

// Process messages. Renderer -> browser: kind and a JSON payload (both
// strings). Browser -> renderer: what to fill (JSON, see FillMessage).
inline constexpr std::string_view kFormsMessage = "brouk.forms";
inline constexpr std::string_view kFormsFillMessage = "brouk.forms.fill";
inline constexpr std::size_t kMaxFormsMessageBytes = 64 * 1024;

// Kinds of messages from the page script.
inline constexpr std::string_view kFormsKindForms = "forms";
inline constexpr std::string_view kFormsKindFocus = "focus";
inline constexpr std::string_view kFormsKindBlur = "blur";
inline constexpr std::string_view kFormsKindSubmit = "submit";
inline constexpr std::string_view kFormsKindGone = "gone";

enum class LoginFieldKind { kUsername, kPassword, kNewPassword, kOneTimeCode };

struct LoginField {
  int id = 0;  // The page script's number for the field.
  LoginFieldKind kind = LoginFieldKind::kUsername;
};

struct LoginForm {
  int id = 0;
  std::vector<LoginField> fields;

  bool Has(LoginFieldKind kind) const;
  std::optional<int> First(LoginFieldKind kind) const;
};

// The login forms of one frame: {"forms":[{"id":1,"fields":[{"id":2,
// "kind":"username"},...]}]}. At most 50 forms of 20 fields.
std::optional<std::vector<LoginForm>> ParseLoginForms(std::string_view json);

// A login field got the focus: {"form":1,"field":2,"kind":"password",
// "x":..,"y":..,"width":..,"height":..} with the field's box in CSS pixels
// of the tab's viewport.
struct FieldFocus {
  int form = 0;
  int field = 0;
  LoginFieldKind kind = LoginFieldKind::kUsername;
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
};
std::optional<FieldFocus> ParseFieldFocus(std::string_view json);

// A login form was submitted: {"form":1,"username":"..","password":"..",
// "new_password":".."}. Any of the values may be empty; at most 1024 bytes
// each.
struct SubmittedLogin {
  int form = 0;
  std::string username;
  std::string password;
  std::string new_password;
};
std::optional<SubmittedLogin> ParseSubmittedLogin(std::string_view json);

// What to offer after a login was submitted and looks successful.
struct SaveDecision {
  enum class Offer { kNothing, kSave, kUpdate };
  Offer offer = Offer::kNothing;
  std::string username;
  std::string password;
  // kUpdate: the entry whose password changes. kNothing: the entry that was
  // used as it is (its use is counted), if any.
  std::optional<std::int64_t> entry_id;
};
// `exact` are the entries saved for the page's origin. A new password (a
// sign-up or password change) wins over the current one. Without a user name
// in the form, the one remembered from the previous step of a multi-step
// login is used. Nothing is offered for "Never for this site" or when
// saving is turned off; updates of existing entries are still offered then
// (Chrome does the same).
SaveDecision DecideSave(const SubmittedLogin& login,
                        const std::vector<PasswordEntry>& exact,
                        std::string_view remembered_username,
                        bool never_for_site,
                        bool offer_to_save);

// The entry filled when the page loads, as in Chrome: only on https (or
// localhost), only with exactly one entry for the exact origin (or exactly
// one with the user name remembered from the previous step), and not with
// "Fill only on click". An index into `exact`.
std::optional<std::size_t> AutofillChoice(
    const std::vector<PasswordEntry>& exact,
    std::string_view remembered_username,
    bool secure_page,
    bool fill_on_click);

// The form to fill: the one with a password field, else (the first step of
// a multi-step login) one with a user name field. nullopt if none.
std::optional<LoginForm> FormToFill(const std::vector<LoginForm>& forms);

// The fill message for `form`: {"fields":{"<id>":"<value>",...}} with the
// user name in its user name field and the password in its (current)
// password field. Empty if the form has neither.
std::string FillMessage(const LoginForm& form,
                        std::string_view username,
                        std::string_view password);
// One value into one field (a generated password into the new-password
// fields of the form, a 2FA code into its field).
std::string FillFieldsMessage(const std::vector<int>& fields,
                              std::string_view value);

// Whether passwords may be filled without a click: https pages and local
// hosts (as Chrome treats localhost as secure).
bool IsSecureOriginForPasswords(std::string_view origin);

}  // namespace brouk

#endif  // BROUK_CORE_PASSWORD_FORMS_H_
