#include "core/password_store.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace brouk {
namespace {

PasswordEntry Entry(std::string origin,
                    std::string username,
                    std::string password) {
  PasswordEntry entry;
  entry.origin = std::move(origin);
  entry.username = std::move(username);
  entry.password = std::move(password);
  return entry;
}

class PasswordStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ =
        std::filesystem::temp_directory_path() /
        ("brouk_passwords_" +
         std::to_string(::testing::UnitTest::GetInstance()->random_seed()) +
         "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
    Reopen();
  }
  void TearDown() override {
    store_.reset();
    std::filesystem::remove_all(dir_);
  }
  void Reopen() {
    store_.reset();
    Result<std::unique_ptr<PasswordStore>> opened =
        PasswordStore::Open(dir_ / "passwords.db", key_);
    ASSERT_TRUE(opened) << opened.error().message;
    store_ = std::move(opened).value();
  }

  std::filesystem::path dir_;
  SecretKey key_ = SecretKey::Generate();
  std::unique_ptr<PasswordStore> store_;
};

TEST_F(PasswordStoreTest, AddsGetsUpdatesAndRemoves) {
  PasswordEntry entry = Entry("https://example.com", "me", "pw1");
  entry.note = "note";
  entry.totp = "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP";
  entry.other_origins = {"https://example.org"};
  Result<std::int64_t> id = store_->Add(entry, 100);
  ASSERT_TRUE(id);
  std::optional<PasswordEntry> saved = store_->Get(id.value());
  ASSERT_TRUE(saved);
  EXPECT_EQ(saved->password, "pw1");
  EXPECT_EQ(saved->note, "note");
  EXPECT_EQ(saved->totp, entry.totp);
  EXPECT_EQ(saved->other_origins, entry.other_origins);
  EXPECT_EQ(saved->created, 100);
  EXPECT_EQ(saved->password_changed, 100);

  saved->username = "me2";
  EXPECT_FALSE(store_->Update(*saved, 200));
  EXPECT_EQ(store_->Get(id.value())->password_changed, 100);  // Same password.
  saved->password = "pw2";
  EXPECT_FALSE(store_->Update(*saved, 300));
  std::optional<PasswordEntry> updated = store_->Get(id.value());
  EXPECT_EQ(updated->username, "me2");
  EXPECT_EQ(updated->password, "pw2");
  EXPECT_EQ(updated->password_changed, 300);

  store_->MarkUsed(id.value(), 400);
  store_->MarkUsed(id.value(), 500);
  EXPECT_EQ(store_->Get(id.value())->times_used, 2);
  EXPECT_EQ(store_->Get(id.value())->last_used, 500);

  std::optional<PasswordEntry> removed = store_->Remove(id.value());
  ASSERT_TRUE(removed);
  EXPECT_FALSE(store_->Get(id.value()));
  EXPECT_FALSE(store_->Remove(id.value()));
  // Undo puts it back with its id and history.
  EXPECT_FALSE(store_->Restore(*removed));
  std::optional<PasswordEntry> restored = store_->Get(id.value());
  ASSERT_TRUE(restored);
  EXPECT_EQ(restored->password, "pw2");
  EXPECT_EQ(restored->times_used, 2);
}

TEST_F(PasswordStoreTest, SecretsAreNotInTheFile) {
  PasswordEntry entry =
      Entry("https://example.com", "visible-user", "SuperSecretPassword123");
  entry.note = "SecretNoteText";
  entry.totp = "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP";
  ASSERT_TRUE(store_->Add(entry, 1));
  store_.reset();
  std::string contents;
  for (const auto& file : std::filesystem::directory_iterator(dir_)) {
    std::ifstream in(file.path(), std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    contents += buffer.str();
  }
  EXPECT_NE(contents.find("visible-user"), std::string::npos);
  EXPECT_EQ(contents.find("SuperSecretPassword123"), std::string::npos);
  EXPECT_EQ(contents.find("SecretNoteText"), std::string::npos);
  EXPECT_EQ(contents.find("JBSWY3DPEHPK3PXP"), std::string::npos);
}

TEST_F(PasswordStoreTest, RefusesAnotherKey) {
  ASSERT_TRUE(store_->Add(Entry("https://a.com", "u", "p"), 1));
  store_.reset();
  Result<std::unique_ptr<PasswordStore>> other =
      PasswordStore::Open(dir_ / "passwords.db", SecretKey::Generate());
  EXPECT_FALSE(other);
  Reopen();
  EXPECT_EQ(store_->All().size(), 1U);
}

TEST_F(PasswordStoreTest, FindsExactAndSameSiteEntries) {
  ASSERT_TRUE(store_->Add(Entry("https://www.company.com", "exact", "1"), 1));
  ASSERT_TRUE(store_->Add(Entry("https://login.company.com", "sub", "2"), 1));
  ASSERT_TRUE(store_->Add(Entry("http://www.company.com", "insecure", "3"), 1));
  ASSERT_TRUE(store_->Add(Entry("https://other.com", "other", "4"), 1));
  PasswordEntry listed = Entry("https://sso.example.net", "listed", "5");
  listed.other_origins = {"https://www.company.com"};
  ASSERT_TRUE(store_->Add(listed, 1));
  ASSERT_TRUE(
      store_->Add(Entry("https://www.company.com:8443", "port", "6"), 1));

  PasswordMatches matches = store_->Find("https://www.company.com");
  ASSERT_EQ(matches.exact.size(), 2U);
  EXPECT_EQ(matches.exact[0].username, "exact");
  EXPECT_EQ(matches.exact[1].username, "listed");
  EXPECT_EQ(matches.exact[0].password, "1");
  ASSERT_EQ(matches.same_site.size(), 3U);
  EXPECT_EQ(matches.same_site[0].username, "insecure");
  EXPECT_EQ(matches.same_site[1].username, "port");
  EXPECT_EQ(matches.same_site[2].username, "sub");

  // Never an https entry on an http page.
  PasswordMatches insecure = store_->Find("http://login.company.com");
  EXPECT_TRUE(insecure.exact.empty());
  ASSERT_EQ(insecure.same_site.size(), 1U);
  EXPECT_EQ(insecure.same_site[0].username, "insecure");

  // Public suffixes are not one site.
  ASSERT_TRUE(store_->Add(Entry("https://alice.github.io", "alice", "7"), 1));
  EXPECT_TRUE(store_->Find("https://bob.github.io").same_site.empty());
}

TEST_F(PasswordStoreTest, ListsAllBySite) {
  ASSERT_TRUE(store_->Add(Entry("https://b.com", "x", "1"), 1));
  ASSERT_TRUE(store_->Add(Entry("https://www.a.com", "y", "2"), 1));
  ASSERT_TRUE(store_->Add(Entry("https://a.com", "z", "3"), 1));
  std::vector<PasswordEntry> all = store_->All();
  ASSERT_EQ(all.size(), 3U);
  EXPECT_EQ(all[0].origin, "https://a.com");
  EXPECT_EQ(all[1].origin, "https://www.a.com");
  EXPECT_EQ(all[2].origin, "https://b.com");
}

TEST_F(PasswordStoreTest, NeverSaveList) {
  EXPECT_FALSE(store_->IsNeverSave("https://a.com"));
  store_->AddNeverSave("https://a.com");
  store_->AddNeverSave("https://a.com");
  store_->AddNeverSave("https://b.com");
  EXPECT_TRUE(store_->IsNeverSave("https://a.com"));
  EXPECT_EQ(store_->NeverSaveOrigins(),
            (std::vector<std::string>{"https://a.com", "https://b.com"}));
  store_->RemoveNeverSave("https://a.com");
  Reopen();
  EXPECT_FALSE(store_->IsNeverSave("https://a.com"));
  EXPECT_TRUE(store_->IsNeverSave("https://b.com"));
}

}  // namespace
}  // namespace brouk
