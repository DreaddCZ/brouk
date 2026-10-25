#include "core/secret_store.h"

#include <gtest/gtest.h>

#include <map>

#include "fake_secret_store.h"

namespace brouk {
namespace {

TEST(SecretStoreTest, CreatesKeyOnceAndReadsItBack) {
  FakeSecretStore store;
  Result<SecretKey> first = LoadOrCreateKey(store, PasswordKeyAccount("p1"));
  ASSERT_TRUE(first);
  EXPECT_EQ(store.items.size(), 1U);
  EXPECT_TRUE(store.items.contains("passwords-key-p1"));
  Result<SecretKey> again = LoadOrCreateKey(store, PasswordKeyAccount("p1"));
  ASSERT_TRUE(again);
  EXPECT_EQ(first.value().bytes(), again.value().bytes());
  Result<SecretKey> other = LoadOrCreateKey(store, PasswordKeyAccount("p2"));
  ASSERT_TRUE(other);
  EXPECT_NE(first.value().bytes(), other.value().bytes());
}

TEST(SecretStoreTest, FailsWhenKeychainFailsOrHoldsGarbage) {
  FakeSecretStore store;
  store.fail = true;
  EXPECT_FALSE(LoadOrCreateKey(store, "a"));
  store.fail = false;
  store.items["a"] = "not a key";
  EXPECT_FALSE(LoadOrCreateKey(store, "a"));
}

}  // namespace
}  // namespace brouk
