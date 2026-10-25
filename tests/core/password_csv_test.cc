#include "core/password_csv.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(PasswordCsvTest, ParsesCsvQuoting) {
  auto rows = ParseCsv(
      "\xEF\xBB\xBF"
      "a,\"b,c\",\"say \"\"hi\"\"\"\r\n"
      "\"multi\nline\",,x\n\n");
  ASSERT_EQ(rows.size(), 2U);
  EXPECT_EQ(rows[0], (std::vector<std::string>{"a", "b,c", "say \"hi\""}));
  EXPECT_EQ(rows[1], (std::vector<std::string>{"multi\nline", "", "x"}));
}

TEST(PasswordCsvTest, ReadsChrome) {
  Result<PasswordCsv> csv = ParsePasswordCsv(
      "name,url,username,password,note\n"
      "example.com,https://example.com/login,me,pw,\"a note\"\n"
      "app,android://abc@com.example/,me,pw,\n"
      "empty,https://e.com/,me,,\n");
  ASSERT_TRUE(csv);
  EXPECT_EQ(csv.value().format, "Chrome");
  ASSERT_EQ(csv.value().entries.size(), 1U);
  const PasswordEntry& entry = csv.value().entries[0];
  EXPECT_EQ(entry.origin, "https://example.com");
  EXPECT_EQ(entry.username, "me");
  EXPECT_EQ(entry.password, "pw");
  EXPECT_EQ(entry.note, "a note");
  EXPECT_EQ(csv.value().problems,
            (std::vector<std::string>{"Row 3: no website address",
                                      "Row 4: no password"}));
}

TEST(PasswordCsvTest, ReadsFirefox) {
  Result<PasswordCsv> csv = ParsePasswordCsv(
      "\"url\",\"username\",\"password\",\"httpRealm\",\"formActionOrigin\","
      "\"guid\",\"timeCreated\",\"timeLastUsed\",\"timePasswordChanged\"\n"
      "\"https://www.mozilla.org\",\"fx\",\"pw\",,\"https://www.mozilla.org\","
      "\"{1}\",\"1700000000000\",\"1700000000000\",\"1700000000000\"\n");
  ASSERT_TRUE(csv);
  EXPECT_EQ(csv.value().format, "Firefox");
  ASSERT_EQ(csv.value().entries.size(), 1U);
  EXPECT_EQ(csv.value().entries[0].origin, "https://www.mozilla.org");
  EXPECT_EQ(csv.value().entries[0].created, 1700000000);
}

TEST(PasswordCsvTest, ReadsSafariWithTotp) {
  Result<PasswordCsv> csv = ParsePasswordCsv(
      "Title,URL,Username,Password,Notes,OTPAuth\n"
      "GitHub,https://github.com/,me,pw,n,"
      "otpauth://totp/GitHub:me?secret=JBSWY3DPEHPK3PXP&issuer=GitHub\n"
      "Bad,https://b.com/,me,pw,,otpauth://totp/x?secret=1\n");
  ASSERT_TRUE(csv);
  EXPECT_EQ(csv.value().format, "Safari");
  ASSERT_EQ(csv.value().entries.size(), 2U);
  EXPECT_TRUE(csv.value().entries[0].totp.starts_with("otpauth://totp/"));
  EXPECT_EQ(csv.value().entries[0].note, "n");
  EXPECT_TRUE(csv.value().entries[1].totp.empty());
  EXPECT_EQ(
      csv.value().problems,
      (std::vector<std::string>{"Row 3: the 2FA secret was not imported"}));
}

TEST(PasswordCsvTest, ReadsBitwardenLoginsOnly) {
  Result<PasswordCsv> csv = ParsePasswordCsv(
      "folder,favorite,type,name,notes,fields,reprompt,login_uri,"
      "login_username,login_password,login_totp\n"
      "Work,,login,Jira,,,0,\"https://jira.corp.com,https://sso.corp.com\","
      "me,pw,JBSWY3DPEHPK3PXP\n"
      ",,note,Secret note,text,,0,,,,\n");
  ASSERT_TRUE(csv);
  EXPECT_EQ(csv.value().format, "Bitwarden");
  ASSERT_EQ(csv.value().entries.size(), 1U);
  const PasswordEntry& entry = csv.value().entries[0];
  EXPECT_EQ(entry.origin, "https://jira.corp.com");
  EXPECT_EQ(entry.other_origins,
            (std::vector<std::string>{"https://sso.corp.com"}));
  EXPECT_FALSE(entry.totp.empty());
  EXPECT_TRUE(csv.value().problems.empty());
}

TEST(PasswordCsvTest, RejectsOtherFiles) {
  EXPECT_FALSE(ParsePasswordCsv(""));
  EXPECT_FALSE(ParsePasswordCsv("a,b,c\n1,2,3\n"));
}

TEST(PasswordCsvTest, WritesChromeFormatThatReadsBack) {
  PasswordEntry entry;
  entry.origin = "https://example.com";
  entry.username = "me";
  entry.password = "p,w\"x";
  entry.note = "line1\nline2";
  std::string text = WritePasswordCsv({entry});
  EXPECT_TRUE(
      text.starts_with("name,url,username,password,note\n"
                       "example.com,https://example.com/,me,"));
  Result<PasswordCsv> csv = ParsePasswordCsv(text);
  ASSERT_TRUE(csv);
  ASSERT_EQ(csv.value().entries.size(), 1U);
  EXPECT_EQ(csv.value().entries[0].password, "p,w\"x");
  EXPECT_EQ(csv.value().entries[0].note, "line1\nline2");
}

TEST(PasswordCsvTest, PlansImportSkippingDuplicates) {
  auto entry = [](std::string origin, std::string user, std::string pw) {
    PasswordEntry e;
    e.origin = std::move(origin);
    e.username = std::move(user);
    e.password = std::move(pw);
    return e;
  };
  std::vector<PasswordEntry> existing = {entry("https://a.com", "me", "1")};
  PasswordImportPlan plan = PlanPasswordImport(
      existing, {entry("https://a.com", "me", "1"),   // duplicate
                 entry("https://a.com", "me", "2"),   // conflict
                 entry("https://a.com", "you", "1"),  // new
                 entry("https://a.com", "you", "1"),  // duplicate in file
                 entry("https://b.com", "me", "1")});
  EXPECT_EQ(plan.to_add.size(), 2U);
  EXPECT_EQ(plan.skipped, 2);
  ASSERT_EQ(plan.conflicts.size(), 1U);
  EXPECT_EQ(plan.conflicts[0].password, "2");
}

}  // namespace
}  // namespace brouk
