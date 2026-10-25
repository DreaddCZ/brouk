#ifndef BROUK_CORE_AUTOFILL_ENTRY_H_
#define BROUK_CORE_AUTOFILL_ENTRY_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brouk {

// Form autofill data (009): entries of contact, address and company details,
// and custom fields. Shared by all profiles.

// The kinds of values an entry holds; also what a recognised form field
// wants. kFullName is only a form field (filled from first, middle and last
// name).
enum class AutofillField {
  kFirstName,
  kMiddleName,
  kLastName,
  kCompany,
  kJobTitle,
  kEmail,
  kPhone,
  kStreet,  // One or more lines, separated by "\n".
  kCity,
  kPostalCode,
  kRegion,
  kCountry,
  kIco,
  kDic,
  kVatId,
  kIban,
  kBic,
  kFullName,
  kStreetLine1,  // Form fields for single street lines.
  kStreetLine2,
};
inline constexpr std::size_t kAutofillStoredFields = 17;  // Up to kBic.

// "first_name", "street_line1", … as the page script and JSON files name
// them; nullopt for unknown names.
std::string_view AutofillFieldName(AutofillField field);
std::optional<AutofillField> AutofillFieldFromName(std::string_view name);
// For the user: "First name", "IČO", …
std::string_view AutofillFieldLabel(AutofillField field);

struct AutofillEntry {
  std::int64_t id = 0;
  std::string label;  // "Work", "Home", "Company A invoicing".
  std::array<std::string, kAutofillStoredFields> values{};
  // Where it was first saved ("eshop.example.cz"), or "" when added by hand.
  std::string origin_site;
  std::int64_t created = 0;  // Seconds since 1970.
  std::int64_t last_used = 0;
  std::int64_t times_used = 0;

  const std::string& Get(AutofillField field) const;
  void Set(AutofillField field, std::string value);
  // The value a form field gets: stored fields as they are, the full name
  // and single street lines composed from the stored ones.
  std::string ValueFor(AutofillField field) const;
  // "Radek, Karlova 1, Praha" for the drop-down.
  std::string Preview() const;
  bool Empty() const;
};

struct CustomField {
  std::int64_t id = 0;
  std::string name;  // "Contract number".
  std::string value;
  // Words in a form field's label that ask for it ("číslo smlouvy").
  std::vector<std::string> label_words;
  std::int64_t last_used = 0;
};

}  // namespace brouk

#endif  // BROUK_CORE_AUTOFILL_ENTRY_H_
