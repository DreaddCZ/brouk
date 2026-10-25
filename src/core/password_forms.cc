#include "core/password_forms.h"

#include <algorithm>
#include <cmath>

#include "core/crypto.h"
#include "core/json.h"
#include "core/web_origin.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxForms = 50;
constexpr std::size_t kMaxFields = 20;
constexpr std::size_t kMaxValueBytes = 1024;

std::optional<LoginFieldKind> KindFromString(std::string_view text) {
  if (text == "username") {
    return LoginFieldKind::kUsername;
  }
  if (text == "password") {
    return LoginFieldKind::kPassword;
  }
  if (text == "new-password") {
    return LoginFieldKind::kNewPassword;
  }
  if (text == "otp") {
    return LoginFieldKind::kOneTimeCode;
  }
  return std::nullopt;
}

// A small positive id, as the page script numbers fields.
std::optional<int> Id(const JsonValue& object, std::string_view key) {
  const JsonValue* value = object.Find(key);
  if (!value || value->kind != JsonValue::Kind::kInteger ||
      value->integer < 1 || value->integer > 1'000'000) {
    return std::nullopt;
  }
  return static_cast<int>(value->integer);
}

std::optional<double> Number(const JsonValue& object, std::string_view key) {
  const JsonValue* value = object.Find(key);
  if (!value) {
    return std::nullopt;
  }
  double number = 0;
  if (value->kind == JsonValue::Kind::kInteger) {
    number = static_cast<double>(value->integer);
  } else if (value->kind == JsonValue::Kind::kDouble) {
    number = value->number;
  } else {
    return std::nullopt;
  }
  if (!std::isfinite(number) || std::abs(number) > 1e6) {
    return std::nullopt;
  }
  return number;
}

std::optional<std::string> Value(const JsonValue& object,
                                 std::string_view key) {
  const JsonValue* value = object.Find(key);
  if (!value) {
    return std::string();
  }
  if (value->kind != JsonValue::Kind::kString ||
      value->string.size() > kMaxValueBytes) {
    return std::nullopt;
  }
  return value->string;
}

std::optional<JsonValue> ParseObject(std::string_view json) {
  if (json.size() > kMaxFormsMessageBytes) {
    return std::nullopt;
  }
  Result<JsonValue> parsed = ParseJsonDocument(json);
  if (!parsed || parsed.value().kind != JsonValue::Kind::kObject) {
    return std::nullopt;
  }
  return std::move(parsed).value();
}

void WipeJson(JsonValue& value) {
  Wipe(value.string);
  for (JsonValue& item : value.items) {
    WipeJson(item);
  }
  for (JsonMember& member : value.members) {
    WipeJson(member.value);
  }
}

}  // namespace

bool LoginForm::Has(LoginFieldKind kind) const {
  return First(kind).has_value();
}

std::optional<int> LoginForm::First(LoginFieldKind kind) const {
  for (const LoginField& field : fields) {
    if (field.kind == kind) {
      return field.id;
    }
  }
  return std::nullopt;
}

std::optional<std::vector<LoginForm>> ParseLoginForms(std::string_view json) {
  std::optional<JsonValue> root = ParseObject(json);
  const JsonValue* forms = root ? root->Find("forms") : nullptr;
  if (!forms || forms->kind != JsonValue::Kind::kArray ||
      forms->items.size() > kMaxForms) {
    return std::nullopt;
  }
  std::vector<LoginForm> result;
  for (const JsonValue& item : forms->items) {
    const JsonValue* fields =
        item.kind == JsonValue::Kind::kObject ? item.Find("fields") : nullptr;
    std::optional<int> id =
        item.kind == JsonValue::Kind::kObject ? Id(item, "id") : std::nullopt;
    if (!id || !fields || fields->kind != JsonValue::Kind::kArray ||
        fields->items.size() > kMaxFields) {
      return std::nullopt;
    }
    LoginForm form;
    form.id = *id;
    for (const JsonValue& field : fields->items) {
      if (field.kind != JsonValue::Kind::kObject) {
        return std::nullopt;
      }
      std::optional<int> field_id = Id(field, "id");
      std::optional<LoginFieldKind> kind =
          KindFromString(field.StringOr("kind", ""));
      if (!field_id || !kind) {
        return std::nullopt;
      }
      form.fields.push_back({*field_id, *kind});
    }
    result.push_back(std::move(form));
  }
  return result;
}

std::optional<FieldFocus> ParseFieldFocus(std::string_view json) {
  std::optional<JsonValue> root = ParseObject(json);
  if (!root) {
    return std::nullopt;
  }
  std::optional<int> form = Id(*root, "form");
  std::optional<int> field = Id(*root, "field");
  std::optional<LoginFieldKind> kind =
      KindFromString(root->StringOr("kind", ""));
  std::optional<double> x = Number(*root, "x");
  std::optional<double> y = Number(*root, "y");
  std::optional<double> width = Number(*root, "width");
  std::optional<double> height = Number(*root, "height");
  if (!form || !field || !kind || !x || !y || !width || !height || *width < 0 ||
      *height < 0) {
    return std::nullopt;
  }
  return FieldFocus{*form, *field, *kind, *x, *y, *width, *height};
}

std::optional<SubmittedLogin> ParseSubmittedLogin(std::string_view json) {
  std::optional<JsonValue> root = ParseObject(json);
  if (!root) {
    return std::nullopt;
  }
  std::optional<int> form = Id(*root, "form");
  std::optional<std::string> username = Value(*root, "username");
  std::optional<std::string> password = Value(*root, "password");
  std::optional<std::string> new_password = Value(*root, "new_password");
  WipeJson(*root);
  if (!form || !username || !password || !new_password) {
    return std::nullopt;
  }
  return SubmittedLogin{*form, std::move(*username), std::move(*password),
                        std::move(*new_password)};
}

SaveDecision DecideSave(const SubmittedLogin& login,
                        const std::vector<PasswordEntry>& exact,
                        std::string_view remembered_username,
                        bool never_for_site,
                        bool offer_to_save) {
  SaveDecision decision;
  decision.password =
      login.new_password.empty() ? login.password : login.new_password;
  decision.username = login.username.empty() ? std::string(remembered_username)
                                             : login.username;
  if (decision.password.empty()) {
    return decision;
  }
  auto same_user = std::ranges::find_if(exact, [&](const PasswordEntry& e) {
    return e.username == decision.username;
  });
  if (same_user != exact.end()) {
    decision.entry_id = same_user->id;
    if (!SecretsEqual(same_user->password, decision.password)) {
      decision.offer = SaveDecision::Offer::kUpdate;
    }
    return decision;
  }
  // A password change without a user name in the form: the only entry
  // whose password was the old one.
  if (!login.new_password.empty() && !login.password.empty()) {
    auto old = std::ranges::find_if(exact, [&](const PasswordEntry& e) {
      return SecretsEqual(e.password, login.password);
    });
    if (old != exact.end()) {
      decision.entry_id = old->id;
      decision.username = old->username;
      decision.offer = SaveDecision::Offer::kUpdate;
      return decision;
    }
  }
  if (!never_for_site && offer_to_save) {
    decision.offer = SaveDecision::Offer::kSave;
  }
  return decision;
}

std::optional<std::size_t> AutofillChoice(
    const std::vector<PasswordEntry>& exact,
    std::string_view remembered_username,
    bool secure_page,
    bool fill_on_click) {
  if (!secure_page || fill_on_click || exact.empty()) {
    return std::nullopt;
  }
  if (!remembered_username.empty()) {
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < exact.size(); ++i) {
      if (exact[i].username == remembered_username) {
        if (found) {
          return std::nullopt;
        }
        found = i;
      }
    }
    if (found) {
      return found;
    }
  }
  if (exact.size() == 1) {
    return 0;
  }
  return std::nullopt;
}

std::optional<LoginForm> FormToFill(const std::vector<LoginForm>& forms) {
  for (const LoginForm& form : forms) {
    if (form.Has(LoginFieldKind::kPassword)) {
      return form;
    }
  }
  for (const LoginForm& form : forms) {
    if (form.Has(LoginFieldKind::kUsername) &&
        !form.Has(LoginFieldKind::kNewPassword)) {
      return form;
    }
  }
  return std::nullopt;
}

std::string FillMessage(const LoginForm& form,
                        std::string_view username,
                        std::string_view password) {
  JsonValue fields = JsonValue::Object();
  std::optional<int> user_field = form.First(LoginFieldKind::kUsername);
  std::optional<int> password_field = form.First(LoginFieldKind::kPassword);
  if (user_field && !username.empty()) {
    fields.Add(std::to_string(*user_field),
               JsonValue::String(std::string(username)));
  }
  if (password_field) {
    fields.Add(std::to_string(*password_field),
               JsonValue::String(std::string(password)));
  }
  if (fields.members.empty()) {
    return {};
  }
  JsonValue root = JsonValue::Object();
  root.Add("fields", std::move(fields));
  std::string message = WriteJsonDocument(root);
  WipeJson(root);
  return message;
}

std::string FillFieldsMessage(const std::vector<int>& fields,
                              std::string_view value) {
  JsonValue values = JsonValue::Object();
  for (int field : fields) {
    values.Add(std::to_string(field), JsonValue::String(std::string(value)));
  }
  JsonValue root = JsonValue::Object();
  root.Add("fields", std::move(values));
  std::string message = WriteJsonDocument(root);
  WipeJson(root);
  return message;
}

bool IsSecureOriginForPasswords(std::string_view origin) {
  if (SchemeOfOrigin(origin) == "https") {
    return true;
  }
  std::string host = HostOfOrigin(origin);
  return host == "localhost" || host.ends_with(".localhost") ||
         host == "127.0.0.1" || host == "[::1]";
}

}  // namespace brouk
