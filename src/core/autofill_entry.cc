#include "core/autofill_entry.h"

#include <algorithm>
#include <utility>

namespace brouk {

namespace {

struct FieldInfo {
  AutofillField field;
  std::string_view name;
  std::string_view label;
};

constexpr std::array<FieldInfo, 20> kFields = {{
    {AutofillField::kFirstName, "first_name", "First name"},
    {AutofillField::kMiddleName, "middle_name", "Middle name"},
    {AutofillField::kLastName, "last_name", "Last name"},
    {AutofillField::kCompany, "company", "Company"},
    {AutofillField::kJobTitle, "job_title", "Job title"},
    {AutofillField::kEmail, "email", "Email"},
    {AutofillField::kPhone, "phone", "Phone"},
    {AutofillField::kStreet, "street", "Street"},
    {AutofillField::kCity, "city", "City"},
    {AutofillField::kPostalCode, "postal_code", "Postal code"},
    {AutofillField::kRegion, "region", "Region"},
    {AutofillField::kCountry, "country", "Country"},
    {AutofillField::kIco, "ico", "IČO"},
    {AutofillField::kDic, "dic", "DIČ"},
    {AutofillField::kVatId, "vat_id", "EU VAT ID"},
    {AutofillField::kIban, "iban", "IBAN"},
    {AutofillField::kBic, "bic", "BIC"},
    {AutofillField::kFullName, "full_name", "Name"},
    {AutofillField::kStreetLine1, "street_line1", "Street, line 1"},
    {AutofillField::kStreetLine2, "street_line2", "Street, line 2"},
}};

const FieldInfo& Info(AutofillField field) {
  return kFields[static_cast<std::size_t>(field)];
}

std::string StreetLine(const std::string& street, std::size_t line) {
  std::size_t start = 0;
  for (std::size_t i = 0; i < line; ++i) {
    std::size_t next = street.find('\n', start);
    if (next == std::string::npos) {
      return {};
    }
    start = next + 1;
  }
  std::size_t end = street.find('\n', start);
  // The second line gets the rest when there are more.
  if (line == 1) {
    std::string rest = street.substr(start);
    std::ranges::replace(rest, '\n', ' ');
    return rest;
  }
  return street.substr(
      start, end == std::string::npos ? std::string::npos : end - start);
}

}  // namespace

std::string_view AutofillFieldName(AutofillField field) {
  return Info(field).name;
}

std::optional<AutofillField> AutofillFieldFromName(std::string_view name) {
  for (const FieldInfo& info : kFields) {
    if (info.name == name) {
      return info.field;
    }
  }
  return std::nullopt;
}

std::string_view AutofillFieldLabel(AutofillField field) {
  return Info(field).label;
}

const std::string& AutofillEntry::Get(AutofillField field) const {
  static const std::string kEmpty;
  auto index = static_cast<std::size_t>(field);
  return index < values.size() ? values[index] : kEmpty;
}

void AutofillEntry::Set(AutofillField field, std::string value) {
  auto index = static_cast<std::size_t>(field);
  if (index < values.size()) {
    values[index] = std::move(value);
  }
}

std::string AutofillEntry::ValueFor(AutofillField field) const {
  switch (field) {
    case AutofillField::kFullName: {
      std::string name;
      for (AutofillField part :
           {AutofillField::kFirstName, AutofillField::kMiddleName,
            AutofillField::kLastName}) {
        if (!Get(part).empty()) {
          name += (name.empty() ? "" : " ") + Get(part);
        }
      }
      return name;
    }
    case AutofillField::kStreetLine1:
      return StreetLine(Get(AutofillField::kStreet), 0);
    case AutofillField::kStreetLine2:
      return StreetLine(Get(AutofillField::kStreet), 1);
    case AutofillField::kStreet: {
      std::string street = Get(AutofillField::kStreet);
      std::ranges::replace(street, '\n', ' ');
      return street;
    }
    default:
      return Get(field);
  }
}

std::string AutofillEntry::Preview() const {
  std::vector<std::string> parts;
  for (AutofillField field :
       {AutofillField::kFirstName, AutofillField::kCompany,
        AutofillField::kStreetLine1, AutofillField::kCity,
        AutofillField::kEmail}) {
    std::string value = field == AutofillField::kFirstName
                            ? ValueFor(AutofillField::kFullName)
                            : ValueFor(field);
    if (!value.empty()) {
      parts.push_back(std::move(value));
    }
    if (parts.size() == 3) {
      break;
    }
  }
  std::string preview;
  for (const std::string& part : parts) {
    preview += (preview.empty() ? "" : ", ") + part;
  }
  return preview;
}

bool AutofillEntry::Empty() const {
  return std::ranges::all_of(values,
                             [](const std::string& v) { return v.empty(); });
}

}  // namespace brouk
