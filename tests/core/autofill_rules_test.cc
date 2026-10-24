#include "core/autofill_rules.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

AutofillEntry Entry(
    std::initializer_list<std::pair<AutofillField, std::string>> values) {
  AutofillEntry entry;
  for (const auto& [field, value] : values) {
    entry.Set(field, value);
  }
  return entry;
}

using F = AutofillField;
using K = AutofillComparison::Kind;

TEST(AutofillRulesTest, NormalizesText) {
  EXPECT_EQ(NormalizeText("  Karlova  1, Praha "), "karlova 1 praha");
  EXPECT_EQ(NormalizeText("Příliš ŽLUŤOUČKÝ kůň"), "prilis zlutoucky kun");
  EXPECT_EQ(NormalizeText("Müller-Lüdenscheidt"), "muller ludenscheidt");
  EXPECT_EQ(NormalizeText("Na Příkopě 12/3"), "na prikope 12 3");
}

TEST(AutofillRulesTest, NormalizesPhonesAndCompactValues) {
  EXPECT_EQ(NormalizePhone("777 123 456", "CZ"), "+420777123456");
  EXPECT_EQ(NormalizePhone("+420 777 123 456", "CZ"), "+420777123456");
  EXPECT_EQ(NormalizePhone("00420777123456", "DE"), "+420777123456");
  EXPECT_EQ(NormalizePhone("030 1234567", "DE"), "+49301234567");
  EXPECT_EQ(NormalizePhone("777123456", ""), "777123456");
  EXPECT_EQ(NormalizeCompact("110 00"), "11000");
  EXPECT_EQ(NormalizeCompact("cz 1234 5678"), "CZ12345678");
  EXPECT_EQ(NormalizeValue(F::kEmail, " Me@Example.COM ", "CZ"),
            "me@example.com");
}

TEST(AutofillRulesTest, EditDistance) {
  EXPECT_EQ(EditDistance("karlova", "karlova", 2), 0U);
  EXPECT_EQ(EditDistance("karlova", "karlvoa", 2), 2U);
  EXPECT_EQ(EditDistance("novak", "novakova", 2), 3U);  // Above the limit.
  EXPECT_EQ(EditDistance("", "ab", 2), 2U);
}

TEST(AutofillRulesTest, SameValuesOfferNothing) {
  std::vector<AutofillEntry> entries = {Entry({{F::kFirstName, "Radek"},
                                               {F::kLastName, "Ševčík"},
                                               {F::kPhone, "+420777123456"},
                                               {F::kStreet, "Karlova 1"},
                                               {F::kPostalCode, "110 00"}})};
  AutofillValues submitted = {{F::kFirstName, "radek"},
                              {F::kLastName, "Sevcik"},
                              {F::kPhone, "777 123 456"},
                              {F::kStreet, "Karlova  1,"},
                              {F::kPostalCode, "11000"}};
  AutofillComparison result = CompareWithEntries(submitted, entries, "CZ");
  EXPECT_EQ(result.kind, K::kSame);
  EXPECT_EQ(result.entry, 0U);
}

TEST(AutofillRulesTest, AddedFieldOffersAddTo) {
  std::vector<AutofillEntry> entries = {
      Entry({{F::kFirstName, "Radek"}, {F::kEmail, "r@example.com"}})};
  AutofillComparison result = CompareWithEntries({{F::kFirstName, "Radek"},
                                                  {F::kEmail, "R@example.com"},
                                                  {F::kPhone, "777123456"}},
                                                 entries, "CZ");
  EXPECT_EQ(result.kind, K::kAddsInformation);
  EXPECT_EQ(result.new_fields, std::vector<F>{F::kPhone});
}

TEST(AutofillRulesTest, SameEmailOrNearNameAndStreetIsSimilar) {
  std::vector<AutofillEntry> entries = {Entry({{F::kFirstName, "Radek"},
                                               {F::kLastName, "Sevcik"},
                                               {F::kEmail, "r@example.com"},
                                               {F::kStreet, "Karlova 1"}})};
  AutofillComparison by_email =
      CompareWithEntries({{F::kFirstName, "Jan"},
                          {F::kEmail, "r@example.com"},
                          {F::kStreet, "Jiná 5"}},
                         entries, "CZ");
  EXPECT_EQ(by_email.kind, K::kSimilar);
  EXPECT_EQ(by_email.changed_fields,
            (std::vector<F>{F::kFirstName, F::kStreet}));

  AutofillComparison by_typo =
      CompareWithEntries({{F::kFirstName, "Radke"},
                          {F::kLastName, "Sevcik"},
                          {F::kStreet, "Karlova 11"},
                          {F::kEmail, "other@example.com"}},
                         entries, "CZ");
  EXPECT_EQ(by_typo.kind, K::kSimilar);

  AutofillComparison different =
      CompareWithEntries({{F::kFirstName, "Eva"},
                          {F::kLastName, "Nováková"},
                          {F::kStreet, "Vodičkova 30"},
                          {F::kEmail, "eva@example.com"}},
                         entries, "CZ");
  EXPECT_EQ(different.kind, K::kDifferent);
  EXPECT_FALSE(different.entry);
}

TEST(AutofillRulesTest, FindsDuplicates) {
  std::vector<AutofillEntry> entries = {
      Entry({{F::kFirstName, "Radek"},
             {F::kStreet, "Karlova 1"},
             {F::kLastName, "Sevcik"}}),
      Entry({{F::kFirstName, "Eva"}, {F::kEmail, "eva@example.com"}}),
      Entry({{F::kFirstName, "Radek"},
             {F::kStreet, "Karlova  1"},
             {F::kLastName, "Ševčík"},
             {F::kPhone, "777123456"}}),
  };
  auto groups = FindDuplicateEntries(entries, "CZ");
  ASSERT_EQ(groups.size(), 1U);
  EXPECT_EQ(groups[0], (std::vector<std::size_t>{0, 2}));
}

TEST(AutofillRulesTest, StoredValuesAndWorthSaving) {
  AutofillValues stored = ToStoredValues({{F::kFullName, "Jan van Berg"},
                                          {F::kStreetLine1, "Main 1"},
                                          {F::kStreetLine2, "Flat 2"},
                                          {F::kCity, "Brno"}});
  EXPECT_EQ(stored[F::kFirstName], "Jan van");
  EXPECT_EQ(stored[F::kLastName], "Berg");
  EXPECT_EQ(stored[F::kStreet], "Main 1\nFlat 2");
  EXPECT_TRUE(WorthSaving(stored));
  EXPECT_FALSE(WorthSaving(
      {{F::kCity, "Brno"}, {F::kCountry, "CZ"}, {F::kRegion, "JM"}}));
  EXPECT_FALSE(WorthSaving({{F::kEmail, "a@b.c"}, {F::kCity, "Brno"}}));
}

TEST(AutofillRulesTest, ValidatesCompanyIdentifiers) {
  // Real-world style examples with valid check digits.
  EXPECT_FALSE(ValidateAutofillValue(F::kIco, "25596641"));
  EXPECT_FALSE(ValidateAutofillValue(F::kIco, "00064581"));
  EXPECT_FALSE(ValidateAutofillValue(F::kIco, "64581"));  // Padded.
  EXPECT_TRUE(ValidateAutofillValue(F::kIco, "25596642"));
  EXPECT_TRUE(ValidateAutofillValue(F::kIco, "abc"));
  EXPECT_FALSE(ValidateAutofillValue(F::kDic, "CZ25596641"));
  EXPECT_FALSE(ValidateAutofillValue(F::kDic, "cz 7001011234"));
  EXPECT_TRUE(ValidateAutofillValue(F::kDic, "CZ1234"));
  EXPECT_FALSE(ValidateAutofillValue(F::kVatId, "DE123456789"));
  EXPECT_FALSE(ValidateAutofillValue(F::kVatId, "ATU12345678"));
  EXPECT_FALSE(ValidateAutofillValue(F::kVatId, "NL123456789B01"));
  EXPECT_TRUE(ValidateAutofillValue(F::kVatId, "DE12345678"));
  EXPECT_TRUE(ValidateAutofillValue(F::kVatId, "XX123"));
  EXPECT_FALSE(
      ValidateAutofillValue(F::kIban, "CZ65 0800 0000 1920 0014 5399"));
  EXPECT_FALSE(ValidateAutofillValue(F::kIban, "DE89370400440532013000"));
  EXPECT_TRUE(ValidateAutofillValue(F::kIban, "CZ65 0800 0000 1920 0014 5398"));
  EXPECT_FALSE(ValidateAutofillValue(F::kIban, ""));
  EXPECT_FALSE(ValidateAutofillValue(F::kCity, "anything"));
}

TEST(AutofillRulesTest, EntryValuesForFormFields) {
  AutofillEntry entry = Entry({{F::kFirstName, "Radek"},
                               {F::kLastName, "Ševčík"},
                               {F::kStreet, "Karlova 1\nFloor 2\nRoom 3"},
                               {F::kCity, "Praha"}});
  EXPECT_EQ(entry.ValueFor(F::kFullName), "Radek Ševčík");
  EXPECT_EQ(entry.ValueFor(F::kStreetLine1), "Karlova 1");
  EXPECT_EQ(entry.ValueFor(F::kStreetLine2), "Floor 2 Room 3");
  EXPECT_EQ(entry.ValueFor(F::kStreet), "Karlova 1 Floor 2 Room 3");
  EXPECT_EQ(entry.Preview(), "Radek Ševčík, Karlova 1, Praha");
  EXPECT_EQ(AutofillFieldFromName("vat_id"), F::kVatId);
  EXPECT_FALSE(AutofillFieldFromName("credit_card"));
}

}  // namespace
}  // namespace brouk
