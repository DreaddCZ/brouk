#ifndef BROUK_CORE_AUTOFILL_FORMS_H_
#define BROUK_CORE_AUTOFILL_FORMS_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/autofill_entry.h"
#include "core/autofill_rules.h"

namespace brouk {

// Autofill's messages from the page script (web/forms, 009), parsed with
// limits (they are untrusted), and the fill messages to it. Kinds of
// kFormsMessage (core/password_forms.h): "afforms", "affocus", "afsubmit".
inline constexpr std::string_view kFormsKindAutofillForms = "afforms";
inline constexpr std::string_view kFormsKindAutofillFocus = "affocus";
inline constexpr std::string_view kFormsKindAutofillSubmit = "afsubmit";

struct AutofillFormField {
  int id = 0;
  std::optional<AutofillField> kind;  // nullopt: only for custom fields.
  std::string hint;                   // Label, placeholder, name.
};
struct AutofillForm {
  int id = 0;
  std::vector<AutofillFormField> fields;
};
// {"forms":[{"id":1,"fields":[{"id":2,"kind":"city","hint":"Město"}]}]}
std::optional<std::vector<AutofillForm>> ParseAutofillForms(
    std::string_view json);

struct AutofillFocus {
  int form = 0;
  int field = 0;
  std::optional<AutofillField> kind;
  std::string hint;
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
};
std::optional<AutofillFocus> ParseAutofillFocus(std::string_view json);

struct AutofillSubmit {
  int form = 0;
  AutofillValues values;  // By form field kind.
};
// {"form":1,"values":{"city":"Praha",…}}
std::optional<AutofillSubmit> ParseAutofillSubmit(std::string_view json);

// {"fields":{"<id>":"<value>",…},"highlight":true}: every recognised
// field of `form` with a value in `entry`.
std::string AutofillFillMessage(const AutofillForm& form,
                                const AutofillEntry& entry,
                                std::vector<int>* filled);
// {"fields":{"<id>":"<value>"}}: one field (a custom field).
std::string AutofillFieldMessage(int field, std::string_view value);
// {"clear":[ids]}
std::string AutofillClearMessage(const std::vector<int>& fields);
// {"focused":"<value>"}: into the focused field (context menu).
std::string AutofillFocusedMessage(std::string_view value);

}  // namespace brouk

#endif  // BROUK_CORE_AUTOFILL_FORMS_H_
