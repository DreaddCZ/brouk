#include "core/bookmark_profile.h"

#include <string>

#include <gtest/gtest.h>

#include "core/profile_export.h"
#include "core/settings.h"
#include "core/yaml_io.h"

namespace brouk {
namespace {

constexpr BookmarkId kBar = BookmarkModel::kBarId;
constexpr BookmarkId kOther = BookmarkModel::kOtherId;
constexpr std::size_t kEnd = BookmarkModel::kEnd;

BookmarkId Folder(BookmarkModel& model,
                  BookmarkId parent,
                  const std::string& name,
                  const ProfileId& profile = "") {
  return model.AddFolder(parent, kEnd, name, profile, 1).value();
}

BookmarkId Page(BookmarkModel& model,
                BookmarkId parent,
                const std::string& name,
                const ProfileId& profile = "") {
  return model
      .AddBookmark(parent, kEnd, name, "https://" + name + ".example/", profile,
                   1)
      .value();
}

const BookmarkItem& Child(const BookmarkModel& model,
                          BookmarkId folder,
                          std::size_t index) {
  return *model.Find(model.Find(folder)->children.at(index));
}

TEST(BookmarkProfileTest, ExportsOnlyTheProfilesItemsWithTheirPath) {
  BookmarkModel model;
  BookmarkId general = Folder(model, kBar, "Team");
  Page(model, general, "jira", "work");
  Page(model, general, "home", "home");
  BookmarkId work = Folder(model, kOther, "Work stuff", "work");
  Page(model, work, "wiki");
  Page(model, kBar, "general");
  YamlNode exported = ExportProfileBookmarks(model, "work");
  ASSERT_EQ(exported.items.size(), 2U);
  std::string yaml = EmitYaml(exported);
  EXPECT_NE(yaml.find("jira"), std::string::npos);
  EXPECT_NE(yaml.find("Team"), std::string::npos);
  EXPECT_NE(yaml.find("wiki"), std::string::npos);
  EXPECT_EQ(yaml.find("home"), std::string::npos);
  EXPECT_EQ(yaml.find("general"), std::string::npos);
  EXPECT_EQ(yaml.find("work\n"), std::string::npos) << "no profile ids";
  EXPECT_TRUE(ExportProfileBookmarks(model, "").items.empty());
}

TEST(BookmarkProfileTest, ImportRecreatesThemLinkedToTheNewProfile) {
  BookmarkModel source;
  BookmarkId team = Folder(source, kBar, "Team");
  BookmarkId sub = Folder(source, team, "Sub");
  Page(source, sub, "jira", "work");
  BookmarkId work = Folder(source, kOther, "Work stuff", "work");
  Page(source, work, "wiki");
  std::string file =
      ExportProfile(Settings(SettingScope::kProfile), "2026-10-23T10:00:00Z",
                    ExportProfileBookmarks(source, "work"));

  Result<ImportedProfile> parsed = ParseProfileExport(file);
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_TRUE(parsed.value().bookmarks);
  EXPECT_TRUE(parsed.value().problems.empty());

  BookmarkModel target;
  BookmarkId existing = Folder(target, kBar, "Team");  // Reused.
  BookmarkId linked_team = Folder(target, kBar, "Sub", "other-profile");
  (void)linked_team;
  ProfileBookmarksImport result =
      ImportProfileBookmarks(target, *parsed.value().bookmarks, "new", 5);
  EXPECT_TRUE(result.problems.empty());
  EXPECT_EQ(result.items, 3);  // jira, Work stuff, wiki.
  ASSERT_EQ(target.Find(existing)->children.size(), 1U);
  const BookmarkItem& created_sub = Child(target, existing, 0);
  EXPECT_EQ(created_sub.name, "Sub");
  EXPECT_EQ(created_sub.profile, "") << "path folders are general";
  const BookmarkItem& jira = Child(target, created_sub.id, 0);
  EXPECT_EQ(jira.url, "https://jira.example/");
  EXPECT_EQ(jira.profile, "new");
  const BookmarkItem& folder = Child(target, kOther, 0);
  EXPECT_EQ(folder.name, "Work stuff");
  EXPECT_EQ(folder.profile, "new");
  EXPECT_EQ(Child(target, folder.id, 0).profile, "") << "follows the folder";
  EXPECT_EQ(target.EffectiveProfile(Child(target, folder.id, 0).id), "new");
}

TEST(BookmarkProfileTest, BadEntriesAreSkippedAndReported) {
  Result<YamlNode> node = ParseYaml(
      "- root: nowhere\n"
      "  item: {type: url, name: a, url: \"https://a.example/\"}\n"
      "- root: bar\n"
      "  item: {type: url, name: b, url: \"https://b.example/\"}\n"
      "- root: bar\n"
      "  item: {type: url, name: c}\n"
      "- just text\n");
  ASSERT_TRUE(node);
  BookmarkModel model;
  ProfileBookmarksImport result =
      ImportProfileBookmarks(model, node.value(), "p", 1);
  EXPECT_EQ(result.items, 1);
  EXPECT_EQ(result.problems.size(), 3U);
  EXPECT_EQ(Child(model, kBar, 0).url, "https://b.example/");
}

}  // namespace
}  // namespace brouk
