#include "core/password_forms.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

PasswordEntry Entry(std::int64_t id, std::string user, std::string password) {
  PasswordEntry entry;
  entry.id = id;
  entry.origin = "https://example.com";
  entry.username = std::move(user);
  entry.password = std::move(password);
  return entry;
}

TEST(PasswordFormsTest, ParsesForms) {
  auto forms = ParseLoginForms(
      R"({"forms":[{"id":1,"fields":[{"id":2,"kind":"username"},)"
      R"({"id":3,"kind":"password"}]},{"id":4,"fields":[{"id":5,"kind":"otp"}]}]})");
  ASSERT_TRUE(forms);
  ASSERT_EQ(forms->size(), 2U);
  EXPECT_EQ((*forms)[0].First(LoginFieldKind::kPassword), 3);
  EXPECT_TRUE((*forms)[1].Has(LoginFieldKind::kOneTimeCode));
  EXPECT_FALSE((*forms)[1].Has(LoginFieldKind::kUsername));
}

TEST(PasswordFormsTest, RejectsBadForms) {
  EXPECT_FALSE(ParseLoginForms("[]"));
  EXPECT_FALSE(ParseLoginForms(R"({"forms":[{"id":0,"fields":[]}]})"));
  EXPECT_FALSE(ParseLoginForms(
      R"({"forms":[{"id":1,"fields":[{"id":2,"kind":"credit-card"}]}]})"));
  EXPECT_FALSE(ParseLoginForms(R"({"forms":[{"id":"1","fields":[]}]})"));
  std::string many = R"({"forms":[)";
  for (int i = 1; i <= 51; ++i) {
    many += (i > 1 ? "," : "") + std::string(R"({"id":)") + std::to_string(i) +
            R"(,"fields":[]})";
  }
  EXPECT_FALSE(ParseLoginForms(many + "]}"));
}

TEST(PasswordFormsTest, ParsesFocusAndSubmit) {
  auto focus = ParseFieldFocus(
      R"({"form":1,"field":3,"kind":"new-password","x":10.5,"y":20,)"
      R"("width":200,"height":30})");
  ASSERT_TRUE(focus);
  EXPECT_EQ(focus->kind, LoginFieldKind::kNewPassword);
  EXPECT_DOUBLE_EQ(focus->x, 10.5);
  EXPECT_FALSE(ParseFieldFocus(
      R"({"form":1,"field":3,"kind":"password","x":1e300,"y":0,"width":1,"height":1})"));

  auto login =
      ParseSubmittedLogin(R"({"form":1,"username":"me","password":"pw"})");
  ASSERT_TRUE(login);
  EXPECT_EQ(login->username, "me");
  EXPECT_EQ(login->password, "pw");
  EXPECT_EQ(login->new_password, "");
  EXPECT_FALSE(ParseSubmittedLogin(R"({"form":1,"password":")" +
                                   std::string(2000, 'x') + R"("})"));
  EXPECT_FALSE(ParseSubmittedLogin(R"({"form":1,"password":5})"));
}

TEST(PasswordFormsTest, OffersToSaveNewLogins) {
  SubmittedLogin login{1, "me", "pw", ""};
  SaveDecision decision = DecideSave(login, {}, "", false, true);
  EXPECT_EQ(decision.offer, SaveDecision::Offer::kSave);
  EXPECT_EQ(decision.username, "me");
  EXPECT_EQ(decision.password, "pw");
  // Never for this site, or saving turned off.
  EXPECT_EQ(DecideSave(login, {}, "", true, true).offer,
            SaveDecision::Offer::kNothing);
  EXPECT_EQ(DecideSave(login, {}, "", false, false).offer,
            SaveDecision::Offer::kNothing);
  // No password, nothing to save.
  EXPECT_EQ(DecideSave({1, "me", "", ""}, {}, "", false, true).offer,
            SaveDecision::Offer::kNothing);
}

TEST(PasswordFormsTest, OffersUpdatesAndCountsUse) {
  std::vector<PasswordEntry> exact = {Entry(7, "me", "old"),
                                      Entry(8, "you", "x")};
  SaveDecision same = DecideSave({1, "me", "old", ""}, exact, "", false, true);
  EXPECT_EQ(same.offer, SaveDecision::Offer::kNothing);
  EXPECT_EQ(same.entry_id, 7);
  SaveDecision changed =
      DecideSave({1, "me", "new", ""}, exact, "", true, false);
  EXPECT_EQ(changed.offer, SaveDecision::Offer::kUpdate);
  EXPECT_EQ(changed.entry_id, 7);
  // A password change form without a user name.
  SaveDecision change =
      DecideSave({1, "", "x", "fresh"}, exact, "", false, true);
  EXPECT_EQ(change.offer, SaveDecision::Offer::kUpdate);
  EXPECT_EQ(change.entry_id, 8);
  EXPECT_EQ(change.username, "you");
  EXPECT_EQ(change.password, "fresh");
}

TEST(PasswordFormsTest, MultiStepUsesRememberedUsername) {
  SaveDecision decision =
      DecideSave({1, "", "pw", ""}, {}, "me@corp.com", false, true);
  EXPECT_EQ(decision.offer, SaveDecision::Offer::kSave);
  EXPECT_EQ(decision.username, "me@corp.com");
}

TEST(PasswordFormsTest, AutofillsOnlyTheSingleExactMatch) {
  std::vector<PasswordEntry> one = {Entry(1, "me", "pw")};
  std::vector<PasswordEntry> two = {Entry(1, "me", "pw"),
                                    Entry(2, "you", "pw2")};
  EXPECT_EQ(AutofillChoice(one, "", true, false), 0U);
  EXPECT_FALSE(AutofillChoice(two, "", true, false));
  EXPECT_FALSE(AutofillChoice(one, "", false, false));  // http
  EXPECT_FALSE(AutofillChoice(one, "", true, true));    // fill on click
  EXPECT_FALSE(AutofillChoice({}, "", true, false));
  // Step 2 of a multi-step login: the user name from step 1 decides.
  EXPECT_EQ(AutofillChoice(two, "you", true, false), 1U);
  EXPECT_EQ(AutofillChoice(one, "other", true, false), 0U);
}

TEST(PasswordFormsTest, ChoosesFormAndBuildsFillMessages) {
  LoginForm search{1, {{2, LoginFieldKind::kOneTimeCode}}};
  LoginForm step1{3, {{4, LoginFieldKind::kUsername}}};
  LoginForm login{
      5, {{6, LoginFieldKind::kUsername}, {7, LoginFieldKind::kPassword}}};
  EXPECT_EQ(FormToFill({search, step1, login})->id, 5);
  EXPECT_EQ(FormToFill({search, step1})->id, 3);
  EXPECT_FALSE(FormToFill({search}));

  EXPECT_EQ(FillMessage(login, "me", "p\"w"),
            "{\n  \"fields\": {\n    \"6\": \"me\",\n    \"7\": \"p\\\"w\"\n  "
            "}\n}\n");
  EXPECT_EQ(FillMessage(step1, "me", "pw"),
            "{\n  \"fields\": {\n    \"4\": \"me\"\n  }\n}\n");
  EXPECT_EQ(FillMessage(search, "me", "pw"), "");
  EXPECT_EQ(
      FillFieldsMessage({8, 9}, "gen"),
      "{\n  \"fields\": {\n    \"8\": \"gen\",\n    \"9\": \"gen\"\n  }\n}\n");
}

TEST(PasswordFormsTest, SecureOrigins) {
  EXPECT_TRUE(IsSecureOriginForPasswords("https://example.com"));
  EXPECT_TRUE(IsSecureOriginForPasswords("http://localhost:3000"));
  EXPECT_TRUE(IsSecureOriginForPasswords("http://127.0.0.1"));
  EXPECT_FALSE(IsSecureOriginForPasswords("http://example.com"));
}

}  // namespace
}  // namespace brouk
