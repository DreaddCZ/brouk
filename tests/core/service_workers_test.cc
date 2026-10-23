#include "core/service_workers.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

ServiceWorkerVersion Version(std::string id,
                             std::string registration,
                             std::string status,
                             std::string script = "https://a.test/sw.js") {
  return {std::move(id), std::move(registration), std::move(script), "running",
          std::move(status)};
}

TEST(ServiceWorkerListTest, ListsRegistrationsWithCurrentVersion) {
  ServiceWorkerList list;
  list.OnRegistrationUpdated({"2", "https://b.test/", false});
  list.OnRegistrationUpdated({"1", "https://a.test/app/", false});
  list.OnVersionUpdated(Version("10", "1", "activated"));
  std::vector<ServiceWorkerEntry> entries = list.Entries();
  ASSERT_EQ(entries.size(), 2u);
  // Sorted by scope.
  EXPECT_EQ(entries[0].scope_url, "https://a.test/app/");
  EXPECT_EQ(entries[0].script_url, "https://a.test/sw.js");
  EXPECT_EQ(entries[0].status, "activated");
  EXPECT_EQ(entries[0].running_status, "running");
  // No version known yet.
  EXPECT_EQ(entries[1].scope_url, "https://b.test/");
  EXPECT_EQ(entries[1].script_url, "");
}

TEST(ServiceWorkerListTest, ActivatedVersionWinsOverNewerWaitingOne) {
  ServiceWorkerList list;
  list.OnRegistrationUpdated({"1", "https://a.test/", false});
  list.OnVersionUpdated(Version("9", "1", "activated", "https://a.test/v1.js"));
  list.OnVersionUpdated(
      Version("10", "1", "installed", "https://a.test/v2.js"));
  EXPECT_EQ(list.Entries().at(0).script_url, "https://a.test/v1.js");
  // The old one becomes redundant once the new one is activated.
  list.OnVersionUpdated(Version("9", "1", "redundant", "https://a.test/v1.js"));
  list.OnVersionUpdated(
      Version("10", "1", "activated", "https://a.test/v2.js"));
  EXPECT_EQ(list.Entries().at(0).script_url, "https://a.test/v2.js");
}

TEST(ServiceWorkerListTest, NewerVersionIdsCompareAsNumbers) {
  ServiceWorkerList list;
  list.OnRegistrationUpdated({"1", "https://a.test/", false});
  list.OnVersionUpdated(
      Version("9", "1", "installing", "https://a.test/old.js"));
  list.OnVersionUpdated(
      Version("10", "1", "installing", "https://a.test/new.js"));
  EXPECT_EQ(list.Entries().at(0).script_url, "https://a.test/new.js");
}

TEST(ServiceWorkerListTest, DeletedRegistrationsAreNotListed) {
  ServiceWorkerList list;
  list.OnRegistrationUpdated({"1", "https://a.test/", false});
  list.OnRegistrationUpdated({"1", "https://a.test/", true});
  EXPECT_TRUE(list.Entries().empty());
}

TEST(ServiceWorkerListTest, IgnoresEntriesWithoutIdsAndStaysBounded) {
  ServiceWorkerList list;
  list.OnRegistrationUpdated({"", "https://a.test/", false});
  list.OnVersionUpdated(Version("", "1", "activated"));
  EXPECT_TRUE(list.Entries().empty());
  for (std::size_t i = 0; i < ServiceWorkerList::kMaxEntries + 10; ++i) {
    list.OnRegistrationUpdated(
        {std::to_string(i), "https://a.test/" + std::to_string(i), false});
  }
  EXPECT_EQ(list.Entries().size(), ServiceWorkerList::kMaxEntries);
  list.Clear();
  EXPECT_TRUE(list.Entries().empty());
}

}  // namespace
}  // namespace brouk
