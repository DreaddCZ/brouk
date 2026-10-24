#include "core/password_profile.h"

#include <gtest/gtest.h>

#include "core/profile_export.h"
#include "core/yaml_io.h"

namespace brouk {
namespace {

constexpr PasswordKdfCost kFast{1, std::size_t{8} * 1024};

std::vector<PasswordEntry> Sample() {
  PasswordEntry entry;
  entry.origin = "https://example.com";
  entry.other_origins = {"https://example.org"};
  entry.username = "me";
  entry.password = "TopSecret!";
  entry.note = "my note";
  entry.totp = "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP";
  entry.created = 5;
  return {entry};
}

TEST(PasswordProfileTest, RoundTripsWithThePassword) {
  YamlNode node = ExportProfilePasswords(Sample(), "export pw", kFast);
  Result<std::vector<PasswordEntry>> imported =
      ImportProfilePasswords(node, "export pw");
  ASSERT_TRUE(imported) << imported.error().message;
  ASSERT_EQ(imported.value().size(), 1U);
  const PasswordEntry& entry = imported.value()[0];
  EXPECT_EQ(entry.origin, "https://example.com");
  EXPECT_EQ(entry.other_origins, Sample()[0].other_origins);
  EXPECT_EQ(entry.password, "TopSecret!");
  EXPECT_EQ(entry.note, "my note");
  EXPECT_EQ(entry.totp, Sample()[0].totp);
  EXPECT_EQ(entry.created, 5);
}

TEST(PasswordProfileTest, NeedsThePassword) {
  YamlNode node = ExportProfilePasswords(Sample(), "export pw", kFast);
  Result<std::vector<PasswordEntry>> wrong =
      ImportProfilePasswords(node, "wrong");
  ASSERT_FALSE(wrong);
  EXPECT_EQ(wrong.error().message, "The export password is wrong.");
  EXPECT_FALSE(ImportProfilePasswords(YamlNode(), "export pw"));
}

TEST(PasswordProfileTest, ExportFileRevealsNothing) {
  YamlNode node = ExportProfilePasswords(Sample(), "export pw", kFast);
  std::string file = ExportProfile(Settings(SettingScope::kProfile),
                                   "2026-10-24T10:00:00Z", YamlNode(), node);
  EXPECT_EQ(file.find("TopSecret"), std::string::npos);
  EXPECT_EQ(file.find("example.com"), std::string::npos);
  EXPECT_EQ(file.find("my note"), std::string::npos);
  Result<ImportedProfile> parsed = ParseProfileExport(file);
  ASSERT_TRUE(parsed);
  ASSERT_TRUE(parsed.value().passwords);
  EXPECT_TRUE(parsed.value().problems.empty());
  Result<std::vector<PasswordEntry>> imported =
      ImportProfilePasswords(*parsed.value().passwords, "export pw");
  ASSERT_TRUE(imported);
  EXPECT_EQ(imported.value()[0].password, "TopSecret!");
}

TEST(PasswordProfileTest, RejectsAbsurdCost) {
  YamlNode node = ExportProfilePasswords(Sample(), "pw", kFast);
  node.GetOrAdd("memory") = YamlNode::Scalar("99999999999999", true);
  EXPECT_FALSE(ImportProfilePasswords(node, "pw"));
}

}  // namespace
}  // namespace brouk
