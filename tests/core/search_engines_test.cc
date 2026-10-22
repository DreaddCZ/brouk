#include "core/search_engines.h"

#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(SearchEnginesTest, BuiltInEngines) {
  Settings global(SettingScope::kGlobal);
  EXPECT_EQ(SearchTemplate(global), "https://www.google.com/search?q=%s");
  ASSERT_FALSE(global.Set("search.engine", std::string("duckduckgo")));
  EXPECT_EQ(SearchTemplate(global), "https://duckduckgo.com/?q=%s");
}

TEST(SearchEnginesTest, CustomTemplate) {
  Settings global(SettingScope::kGlobal);
  ASSERT_FALSE(global.Set("search.engine", std::string("custom")));
  // Not set yet: Google.
  EXPECT_EQ(SearchTemplate(global), "https://www.google.com/search?q=%s");
  ASSERT_FALSE(global.Set("search.custom_template",
                          std::string("https://kagi.com/search?q=%s")));
  EXPECT_EQ(SearchTemplate(global), "https://kagi.com/search?q=%s");
  EXPECT_EQ(SearchEngineName(global), "kagi.com");
}

TEST(SearchEnginesTest, Names) {
  Settings global(SettingScope::kGlobal);
  EXPECT_EQ(SearchEngineName(global), "Google");
  ASSERT_FALSE(global.Set("search.engine", std::string("duckduckgo")));
  EXPECT_EQ(SearchEngineName(global), "DuckDuckGo");
  ASSERT_FALSE(global.Set("search.engine", std::string("custom")));
  ASSERT_FALSE(global.Set("search.custom_template",
                          std::string("https://www.example.org:8080/s?q=%s")));
  EXPECT_EQ(SearchEngineName(global), "example.org");
}

TEST(SearchEnginesTest, TemplateAndFolderFormatsAreChecked) {
  Settings global(SettingScope::kGlobal);
  EXPECT_TRUE(global.Set("search.custom_template",
                         std::string("https://example.com/search")));
  EXPECT_TRUE(global.Set("search.custom_template",
                         std::string("javascript:alert(%s)")));
  EXPECT_FALSE(global.Set("search.custom_template", std::string()));
  EXPECT_FALSE(global.Set("downloads.folder", std::string("~/Work")));
  EXPECT_FALSE(global.Set("downloads.folder", std::string("/Volumes/x")));
  EXPECT_TRUE(global.Set("downloads.folder", std::string("Downloads")));
  EXPECT_TRUE(global.Set("profiles.default", std::string("Work")));
}

}  // namespace
}  // namespace brouk
