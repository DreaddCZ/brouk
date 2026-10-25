#include "core/autofill_forms.h"

#include <cmath>

#include "core/json.h"
#include "core/password_forms.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxForms = 50;
constexpr std::size_t kMaxFields = 60;
constexpr std::size_t kMaxText = 1000;

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
  double number = value->kind == JsonValue::Kind::kInteger
                      ? static_cast<double>(value->integer)
                  : value->kind == JsonValue::Kind::kDouble ? value->number
                                                            : NAN;
  if (!std::isfinite(number) || std::abs(number) > 1e6) {
    return std::nullopt;
  }
  return number;
}

// "" -> nullopt kind (custom only); an unknown name -> invalid.
std::optional<std::optional<AutofillField>> Kind(const JsonValue& object) {
  const JsonValue* value = object.Find("kind");
  if (!value || value->kind != JsonValue::Kind::kString) {
    return std::nullopt;
  }
  if (value->string.empty()) {
    return std::optional<AutofillField>();
  }
  std::optional<AutofillField> kind = AutofillFieldFromName(value->string);
  if (!kind) {
    return std::nullopt;
  }
  return kind;
}

std::string Hint(const JsonValue& object) {
  return object.StringOr("hint", "").substr(0, 200);
}

std::string Write(const JsonValue& root) {
  return WriteJsonDocument(root);
}

}  // namespace

std::optional<std::vector<AutofillForm>> ParseAutofillForms(
    std::string_view json) {
  std::optional<JsonValue> root = ParseObject(json);
  const JsonValue* forms = root ? root->Find("forms") : nullptr;
  if (!forms || forms->kind != JsonValue::Kind::kArray ||
      forms->items.size() > kMaxForms) {
    return std::nullopt;
  }
  std::vector<AutofillForm> result;
  for (const JsonValue& item : forms->items) {
    if (item.kind != JsonValue::Kind::kObject) {
      return std::nullopt;
    }
    std::optional<int> id = Id(item, "id");
    const JsonValue* fields = item.Find("fields");
    if (!id || !fields || fields->kind != JsonValue::Kind::kArray ||
        fields->items.size() > kMaxFields) {
      return std::nullopt;
    }
    AutofillForm form;
    form.id = *id;
    for (const JsonValue& field : fields->items) {
      if (field.kind != JsonValue::Kind::kObject) {
        return std::nullopt;
      }
      std::optional<int> field_id = Id(field, "id");
      std::optional<std::optional<AutofillField>> kind = Kind(field);
      if (!field_id || !kind) {
        return std::nullopt;
      }
      form.fields.push_back({*field_id, *kind, Hint(field)});
    }
    result.push_back(std::move(form));
  }
  return result;
}

std::optional<AutofillFocus> ParseAutofillFocus(std::string_view json) {
  std::optional<JsonValue> root = ParseObject(json);
  if (!root) {
    return std::nullopt;
  }
  std::optional<int> form = Id(*root, "form");
  std::optional<int> field = Id(*root, "field");
  std::optional<std::optional<AutofillField>> kind = Kind(*root);
  std::optional<double> x = Number(*root, "x");
  std::optional<double> y = Number(*root, "y");
  std::optional<double> width = Number(*root, "width");
  std::optional<double> height = Number(*root, "height");
  if (!form || !field || !kind || !x || !y || !width || !height || *width < 0 ||
      *height < 0) {
    return std::nullopt;
  }
  return AutofillFocus{*form, *field, *kind,  Hint(*root),
                       *x,    *y,     *width, *height};
}

std::optional<AutofillSubmit> ParseAutofillSubmit(std::string_view json) {
  std::optional<JsonValue> root = ParseObject(json);
  if (!root) {
    return std::nullopt;
  }
  std::optional<int> form = Id(*root, "form");
  const JsonValue* values = root->Find("values");
  if (!form || !values || values->kind != JsonValue::Kind::kObject ||
      values->members.size() > kMaxFields) {
    return std::nullopt;
  }
  AutofillSubmit submit;
  submit.form = *form;
  for (const JsonMember& member : values->members) {
    std::optional<AutofillField> kind = AutofillFieldFromName(member.key);
    if (!kind || member.value.kind != JsonValue::Kind::kString) {
      return std::nullopt;
    }
    submit.values[*kind] = member.value.string.substr(0, kMaxText);
  }
  return submit;
}

std::string AutofillFillMessage(const AutofillForm& form,
                                const AutofillEntry& entry,
                                std::vector<int>* filled) {
  JsonValue fields = JsonValue::Object();
  for (const AutofillFormField& field : form.fields) {
    if (!field.kind) {
      continue;
    }
    std::string value = entry.ValueFor(*field.kind);
    if (value.empty()) {
      continue;
    }
    fields.Add(std::to_string(field.id), JsonValue::String(std::move(value)));
    if (filled) {
      filled->push_back(field.id);
    }
  }
  JsonValue root = JsonValue::Object();
  root.Add("fields", std::move(fields));
  root.Add("highlight", JsonValue::Bool(true));
  return Write(root);
}

std::string AutofillFieldMessage(int field, std::string_view value) {
  JsonValue fields = JsonValue::Object();
  fields.Add(std::to_string(field), JsonValue::String(std::string(value)));
  JsonValue root = JsonValue::Object();
  root.Add("fields", std::move(fields));
  return Write(root);
}

std::string AutofillClearMessage(const std::vector<int>& fields) {
  JsonValue list = JsonValue::Array();
  for (int field : fields) {
    list.items.push_back(JsonValue::Integer(field));
  }
  JsonValue root = JsonValue::Object();
  root.Add("clear", std::move(list));
  return Write(root);
}

std::string AutofillFocusedMessage(std::string_view value) {
  JsonValue root = JsonValue::Object();
  root.Add("focused", JsonValue::String(std::string(value)));
  return Write(root);
}

}  // namespace brouk
