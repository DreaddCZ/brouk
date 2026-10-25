#include "core/autofill_store.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace brouk {
namespace {

using F = AutofillField;

class AutofillStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ =
        std::filesystem::temp_directory_path() /
        ("brouk_autofill_" +
         std::string(
             ::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
    Result<std::unique_ptr<AutofillStore>> store =
        AutofillStore::Open(dir_ / "autofill.db", key_);
    ASSERT_TRUE(store) << store.error().message;
    store_ = std::move(store).value();
  }
  void TearDown() override {
    store_.reset();
    std::filesystem::remove_all(dir_);
  }

  std::filesystem::path dir_;
  SecretKey key_ = SecretKey::Generate();
  std::unique_ptr<AutofillStore> store_;
};

AutofillEntry Sample() {
  AutofillEntry entry;
  entry.label = "Work";
  entry.Set(F::kFirstName, "Radek");
  entry.Set(F::kIban, "CZ6508000000192000145399");
  entry.Set(F::kStreet, "Karlova 1");
  entry.origin_site = "eshop.example.cz";
  return entry;
}

TEST_F(AutofillStoreTest, EntriesRoundTripAndUndo) {
  Result<std::int64_t> id = store_->AddEntry(Sample(), 100);
  ASSERT_TRUE(id);
  std::vector<AutofillEntry> all = store_->Entries();
  ASSERT_EQ(all.size(), 1U);
  EXPECT_EQ(all[0].label, "Work");
  EXPECT_EQ(all[0].Get(F::kIban), "CZ6508000000192000145399");
  EXPECT_EQ(all[0].origin_site, "eshop.example.cz");
  EXPECT_EQ(all[0].created, 100);
  all[0].Set(F::kCity, "Praha");
  EXPECT_FALSE(store_->UpdateEntry(all[0]));
  store_->MarkEntryUsed(id.value(), 200);
  EXPECT_EQ(store_->Entry(id.value())->Get(F::kCity), "Praha");
  EXPECT_EQ(store_->Entry(id.value())->times_used, 1);
  std::vector<std::int64_t> ids = {id.value()};
  std::vector<AutofillEntry> removed = store_->RemoveEntries(ids);
  ASSERT_EQ(removed.size(), 1U);
  EXPECT_TRUE(store_->Entries().empty());
  store_->RestoreEntries(removed);
  EXPECT_EQ(store_->Entry(id.value())->times_used, 1);
}

TEST_F(AutofillStoreTest, ValuesAreNotInTheFile) {
  ASSERT_TRUE(store_->AddEntry(Sample(), 1));
  CustomField field{0, "Contract number", "SML-2026-77", {"smlouva"}, 0};
  ASSERT_TRUE(store_->AddCustomField(field));
  store_.reset();
  std::string contents;
  for (const auto& file : std::filesystem::directory_iterator(dir_)) {
    std::ifstream in(file.path(), std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    contents += buffer.str();
  }
  for (const char* secret : {"Radek", "CZ6508000000192000145399", "Karlova",
                             "eshop.example.cz", "SML-2026-77", "Work"}) {
    EXPECT_EQ(contents.find(secret), std::string::npos) << secret;
  }
  EXPECT_FALSE(
      AutofillStore::Open(dir_ / "autofill.db", SecretKey::Generate()));
}

TEST_F(AutofillStoreTest, CustomFields) {
  CustomField field{0, "Cost centre", "4711", {"stredisko", "cost centre"}, 0};
  Result<std::int64_t> id = store_->AddCustomField(field);
  ASSERT_TRUE(id);
  std::vector<CustomField> all = store_->CustomFields();
  ASSERT_EQ(all.size(), 1U);
  EXPECT_EQ(all[0].label_words.size(), 2U);
  all[0].value = "4712";
  EXPECT_FALSE(store_->UpdateCustomField(all[0]));
  std::vector<std::int64_t> ids = {id.value()};
  std::vector<CustomField> removed = store_->RemoveCustomFields(ids);
  EXPECT_TRUE(store_->CustomFields().empty());
  store_->RestoreCustomFields(removed);
  EXPECT_EQ(store_->CustomFields()[0].value, "4712");
}

TEST(AutofillJsonTest, RoundTripsAndPlansImport) {
  AutofillEntry saved = Sample();
  saved.id = 7;
  CustomField field{0, "Cost centre", "4711", {"cost centre"}, 0};
  std::string json = WriteAutofillJson({saved}, {field});
  Result<AutofillJson> parsed = ParseAutofillJson(json);
  ASSERT_TRUE(parsed);
  ASSERT_EQ(parsed.value().entries.size(), 1U);
  EXPECT_EQ(parsed.value().entries[0].Get(F::kIban), saved.Get(F::kIban));
  EXPECT_FALSE(ParseAutofillJson("{\"format\":\"other\"}"));

  AutofillJson incoming = parsed.value();
  AutofillEntry more = Sample();
  more.Set(F::kPhone, "777123456");  // Adds a phone to the saved entry.
  AutofillEntry other;
  other.Set(F::kFirstName, "Eva");
  other.Set(F::kEmail, "eva@example.com");
  incoming.entries.push_back(more);
  incoming.entries.push_back(other);
  incoming.entries.push_back(other);  // Twice in the file.
  AutofillImportPlan plan =
      PlanAutofillImport(incoming, {saved}, {field}, "CZ");
  EXPECT_EQ(plan.skipped, 2);  // The same entry, and Eva's second copy.
  ASSERT_EQ(plan.merged.size(), 1U);
  EXPECT_EQ(plan.merged[0].id, 7);
  EXPECT_EQ(plan.merged[0].Get(F::kPhone), "777123456");
  ASSERT_EQ(plan.to_add.size(), 1U);
  EXPECT_EQ(plan.to_add[0].Get(F::kFirstName), "Eva");
  EXPECT_EQ(plan.custom_skipped, 1);
  EXPECT_TRUE(plan.custom_to_add.empty());
}

}  // namespace
}  // namespace brouk
