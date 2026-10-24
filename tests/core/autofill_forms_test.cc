#include "core/autofill_forms.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(AutofillFormsTest, ParsesForms) {
  auto forms = ParseAutofillForms(
      R"({"forms":[{"id":1,"fields":[{"id":2,"kind":"city","hint":"Město"},)"
      R"({"id":3,"kind":"","hint":"Číslo smlouvy"}]}]})");
  ASSERT_TRUE(forms);
  ASSERT_EQ((*forms)[0].fields.size(), 2U);
  EXPECT_EQ((*forms)[0].fields[0].kind, AutofillField::kCity);
  EXPECT_FALSE((*forms)[0].fields[1].kind);
  EXPECT_EQ((*forms)[0].fields[1].hint, "Číslo smlouvy");
  EXPECT_FALSE(ParseAutofillForms(
      R"({"forms":[{"id":1,"fields":[{"id":2,"kind":"card_number"}]}]})"));
  EXPECT_FALSE(ParseAutofillForms(R"({"forms":[{"id":-1,"fields":[]}]})"));
}

TEST(AutofillFormsTest, ParsesFocusAndSubmit) {
  auto focus = ParseAutofillFocus(
      R"({"form":1,"field":2,"kind":"ico","hint":"IČO","x":1,"y":2,"width":3,"height":4})");
  ASSERT_TRUE(focus);
  EXPECT_EQ(focus->kind, AutofillField::kIco);
  auto submit = ParseAutofillSubmit(
      R"({"form":1,"values":{"city":"Praha","full_name":"Jan Novák"}})");
  ASSERT_TRUE(submit);
  EXPECT_EQ(submit->values.at(AutofillField::kFullName), "Jan Novák");
  EXPECT_FALSE(ParseAutofillSubmit(R"({"form":1,"values":{"cvv":"123"}})"));
}

TEST(AutofillFormsTest, BuildsFillMessages) {
  AutofillForm form{1,
                    {{2, AutofillField::kFullName, ""},
                     {3, AutofillField::kPhone, ""},
                     {4, std::nullopt, "Note"}}};
  AutofillEntry entry;
  entry.Set(AutofillField::kFirstName, "Jan");
  entry.Set(AutofillField::kLastName, "Novák");
  std::vector<int> filled;
  EXPECT_EQ(AutofillFillMessage(form, entry, &filled),
            "{\n  \"fields\": {\n    \"2\": \"Jan Novák\"\n  },\n  "
            "\"highlight\": true\n}\n");
  EXPECT_EQ(filled, std::vector<int>{2});
  EXPECT_EQ(AutofillClearMessage({2, 3}),
            "{\n  \"clear\": [\n    2,\n    3\n  ]\n}\n");
  EXPECT_EQ(AutofillFocusedMessage("x"), "{\n  \"focused\": \"x\"\n}\n");
}

}  // namespace
}  // namespace brouk
