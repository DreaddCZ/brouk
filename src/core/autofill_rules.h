#ifndef BROUK_CORE_AUTOFILL_RULES_H_
#define BROUK_CORE_AUTOFILL_RULES_H_

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/autofill_entry.h"

namespace brouk {

// The rules of 009 that decide what counts as the same value, what to offer
// after a form was submitted, and which company identifiers are valid. Pure
// functions; stored values keep the user's formatting, only comparisons use
// the normalised forms.

// Lower case, without accents and punctuation, spaces trimmed and
// collapsed: "  Karlova  1, Praha " -> "karlova 1 praha".
std::string NormalizeText(std::string_view text);
// International form: "777 123 456" with country "CZ" -> "+420777123456";
// "00420…" and "+420 …" too. Without a known country, digits only.
std::string NormalizePhone(std::string_view text, std::string_view country);
// Spaces removed, upper case (postal codes, IČO, DIČ, VAT ID, IBAN, BIC).
std::string NormalizeCompact(std::string_view text);
// The form used to compare values of `field`.
std::string NormalizeValue(AutofillField field,
                           std::string_view value,
                           std::string_view country);

// Edits (insert, delete, replace) between two strings, stopping early once
// above `limit` (returns limit + 1 then).
std::size_t EditDistance(std::string_view a,
                         std::string_view b,
                         std::size_t limit);

// The calling code of a region ("CZ" -> "420"); "" when unknown.
std::string_view CountryCallingCode(std::string_view region);

// What Brouk offers after a form with these values was submitted.
struct AutofillComparison {
  enum class Kind { kSame, kAddsInformation, kSimilar, kDifferent };
  Kind kind = Kind::kDifferent;
  // kSame, kAddsInformation, kSimilar: the entry concerned (an index).
  std::optional<std::size_t> entry;
  // kAddsInformation and kSimilar: fields the form adds.
  std::vector<AutofillField> new_fields;
  // kSimilar: fields whose values differ.
  std::vector<AutofillField> changed_fields;
};
using AutofillValues = std::map<AutofillField, std::string>;
// `submitted` holds stored fields only (see ToStoredValues).
AutofillComparison CompareWithEntries(const AutofillValues& submitted,
                                      const std::vector<AutofillEntry>& entries,
                                      std::string_view country);
// Whether two entries count as duplicates (the same rules: same, adds
// information or similar).
bool AreSimilarEntries(const AutofillEntry& a,
                       const AutofillEntry& b,
                       std::string_view country);
// Groups of similar entries (indices), for "Find duplicates".
std::vector<std::vector<std::size_t>> FindDuplicateEntries(
    const std::vector<AutofillEntry>& entries,
    std::string_view country);

// Submitted form values by form field kind -> stored fields: a full name is
// split into first and last name, street lines are joined.
AutofillValues ToStoredValues(const AutofillValues& form_values);
// Whether a submitted form is worth offering to save: at least three
// recognised fields, one of them a name, email, phone or street.
bool WorthSaving(const AutofillValues& stored_values);

// Whether a custom field suits a form field whose label, placeholder and
// name are `hint`: one of its label words (or its name) appears in it,
// compared as normalised text.
bool CustomFieldMatches(const CustomField& field, std::string_view hint);

// Validation: nullopt when the value is valid (or empty), else the problem
// for the user. IČO checksum (mod 11), Czech DIČ (CZ + 8-10 digits), EU VAT
// ID formats by country, IBAN checksum (mod 97).
std::optional<std::string> ValidateAutofillValue(AutofillField field,
                                                 std::string_view value);

}  // namespace brouk

#endif  // BROUK_CORE_AUTOFILL_RULES_H_
