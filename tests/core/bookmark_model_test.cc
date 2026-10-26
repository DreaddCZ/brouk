#include "core/bookmark_model.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace brouk {
namespace {

constexpr BookmarkId kBar = BookmarkModel::kBarId;
constexpr BookmarkId kOther = BookmarkModel::kOtherId;
constexpr std::size_t kEnd = BookmarkModel::kEnd;

BookmarkId AddUrl(BookmarkModel& model,
                  BookmarkId parent,
                  const std::string& url,
                  const ProfileId& profile = "") {
  Result<BookmarkId> id =
      model.AddBookmark(parent, kEnd, url, url, profile, 100);
  EXPECT_TRUE(id);
  return id ? id.value() : 0;
}

BookmarkId AddDir(BookmarkModel& model,
                  BookmarkId parent,
                  const std::string& name,
                  const ProfileId& profile = "") {
  Result<BookmarkId> id = model.AddFolder(parent, kEnd, name, profile, 100);
  EXPECT_TRUE(id);
  return id ? id.value() : 0;
}

TEST(BookmarkModelTest, StartsWithTheTwoTopLevelFolders) {
  BookmarkModel model;
  ASSERT_NE(model.Find(kBar), nullptr);
  ASSERT_NE(model.Find(kOther), nullptr);
  EXPECT_TRUE(model.Find(kBar)->folder);
  EXPECT_TRUE(model.Find(kBar)->children.empty());
  EXPECT_TRUE(model.Rename(kBar, "x"));
  EXPECT_TRUE(model.SetProfile(kOther, "work"));
  EXPECT_TRUE(model.Move(kBar, kOther, 0));
  EXPECT_FALSE(model.Remove(kOther));
}

TEST(BookmarkModelTest, AllUrlsListsEveryBookmarkOnce) {
  BookmarkModel model;
  AddUrl(model, kBar, "https://b.com/");
  BookmarkId folder = AddDir(model, kOther, "Work", "p-work");
  AddUrl(model, folder, "https://a.com/");
  AddUrl(model, kOther, "https://b.com/");
  EXPECT_EQ(model.AllUrls(),
            (std::vector<std::string>{"https://a.com/", "https://b.com/"}));
}

TEST(BookmarkModelTest, AddsAtTheEndOrAtAnIndex) {
  BookmarkModel model;
  BookmarkId a = AddUrl(model, kBar, "https://a.com/");
  BookmarkId b = AddUrl(model, kBar, "https://b.com/");
  Result<BookmarkId> c =
      model.AddBookmark(kBar, 0, "C", "https://c.com/", "", 1);
  ASSERT_TRUE(c);
  EXPECT_EQ(model.Find(kBar)->children,
            (std::vector<BookmarkId>{c.value(), a, b}));
  EXPECT_FALSE(model.AddBookmark(a, kEnd, "x", "https://x.com/", "", 1));
  EXPECT_FALSE(model.AddBookmark(kBar, kEnd, "x", "", "", 1));
  EXPECT_FALSE(model.AddBookmark(kBar, kEnd, "x", "https://x.com/", "BAD", 1));
  EXPECT_FALSE(model.AddFolder(999, kEnd, "x", "", 1));
}

TEST(BookmarkModelTest, MovesSeveralItemsInOrder) {
  BookmarkModel model;
  BookmarkId a = AddUrl(model, kBar, "https://a.com/");
  BookmarkId b = AddUrl(model, kBar, "https://b.com/");
  BookmarkId c = AddUrl(model, kBar, "https://c.com/");
  BookmarkId d = AddUrl(model, kBar, "https://d.com/");
  // c and a before b, in the order selected.
  EXPECT_FALSE(model.MoveItems({c, a}, kBar, 1));
  EXPECT_EQ(model.Find(kBar)->children, (std::vector<BookmarkId>{c, a, b, d}));
  BookmarkId folder = AddDir(model, kOther, "F");
  EXPECT_FALSE(model.MoveItems({b, d}, folder, kEnd));
  EXPECT_EQ(model.Find(folder)->children, (std::vector<BookmarkId>{b, d}));
  EXPECT_TRUE(model.MoveItems({folder}, folder, 0)) << "into itself";
}

TEST(BookmarkModelTest, ItemsInsideMovedFoldersGoAlong) {
  BookmarkModel model;
  BookmarkId folder = AddDir(model, kBar, "F");
  BookmarkId inside = AddUrl(model, folder, "https://in.com/");
  BookmarkId other = AddUrl(model, kBar, "https://o.com/");
  EXPECT_EQ(model.WithoutNested({inside, folder, other, 999}),
            (std::vector<BookmarkId>{folder, other}));
  EXPECT_FALSE(model.MoveItems({inside, folder}, kOther, kEnd));
  EXPECT_EQ(model.Find(inside)->parent, folder);
  EXPECT_EQ(model.Find(kOther)->children, (std::vector<BookmarkId>{folder}));
}

TEST(BookmarkModelTest, NestedFoldersAndMoving) {
  BookmarkModel model;
  BookmarkId outer = AddDir(model, kBar, "Outer");
  BookmarkId inner = AddDir(model, outer, "Inner");
  BookmarkId page = AddUrl(model, inner, "https://a.com/");
  EXPECT_TRUE(model.IsInside(page, outer));
  EXPECT_FALSE(model.IsInside(outer, page));
  EXPECT_TRUE(model.Move(outer, inner, 0)) << "into itself";
  EXPECT_TRUE(model.Move(outer, outer, 0));
  EXPECT_FALSE(model.Move(page, kOther, kEnd));
  EXPECT_EQ(model.Find(page)->parent, kOther);
  EXPECT_TRUE(model.Find(inner)->children.empty());
}

TEST(BookmarkModelTest, MovingWithinAFolderCountsIndexBeforeTheMove) {
  BookmarkModel model;
  BookmarkId a = AddUrl(model, kBar, "https://a.com/");
  BookmarkId b = AddUrl(model, kBar, "https://b.com/");
  BookmarkId c = AddUrl(model, kBar, "https://c.com/");
  // Dropping a after c: index 3 as the bar was before the move.
  EXPECT_FALSE(model.Move(a, kBar, 3));
  EXPECT_EQ(model.Find(kBar)->children, (std::vector<BookmarkId>{b, c, a}));
  EXPECT_FALSE(model.Move(a, kBar, 0));
  EXPECT_EQ(model.Find(kBar)->children, (std::vector<BookmarkId>{a, b, c}));
  EXPECT_FALSE(model.Move(c, kBar, 1));
  EXPECT_EQ(model.Find(kBar)->children, (std::vector<BookmarkId>{a, c, b}));
}

TEST(BookmarkModelTest, ALinkedFolderLinksEverythingInside) {
  BookmarkModel model;
  BookmarkId work = AddDir(model, kBar, "Work", "work");
  BookmarkId page = AddUrl(model, work, "https://jira.example/", "home");
  EXPECT_EQ(model.Find(page)->profile, "") << "the folder's link applies";
  EXPECT_EQ(model.EffectiveProfile(page), "work");
  EXPECT_EQ(model.InheritedProfile(page), "work");
  EXPECT_TRUE(model.SetProfile(page, "home")) << "locked to the folder";
  EXPECT_TRUE(model.IsVisibleIn(page, "work"));
  EXPECT_FALSE(model.IsVisibleIn(page, "home"));
  EXPECT_FALSE(model.IsVisibleIn(work, "home"));
}

TEST(BookmarkModelTest, MovingIntoALinkedFolderLinksAndOutMakesGeneral) {
  BookmarkModel model;
  BookmarkId work = AddDir(model, kBar, "Work", "work");
  BookmarkId general = AddDir(model, kBar, "General");
  BookmarkId own = AddUrl(model, general, "https://a.com/", "home");
  BookmarkId sub = AddDir(model, general, "Sub");
  BookmarkId deep = AddUrl(model, sub, "https://b.com/", "home");
  EXPECT_FALSE(model.Move(general, work, kEnd));
  EXPECT_EQ(model.EffectiveProfile(own), "work");
  EXPECT_EQ(model.EffectiveProfile(deep), "work");
  EXPECT_FALSE(model.Move(general, kOther, kEnd));
  EXPECT_EQ(model.EffectiveProfile(general), "");
  EXPECT_EQ(model.EffectiveProfile(own), "") << "own links were dropped";
  EXPECT_EQ(model.EffectiveProfile(deep), "");
}

TEST(BookmarkModelTest, LinkingAFolderUnlinksItsContents) {
  BookmarkModel model;
  BookmarkId folder = AddDir(model, kBar, "F");
  BookmarkId page = AddUrl(model, folder, "https://a.com/", "home");
  EXPECT_FALSE(model.SetProfile(folder, "work"));
  EXPECT_EQ(model.Find(page)->profile, "");
  EXPECT_EQ(model.EffectiveProfile(page), "work");
  EXPECT_FALSE(model.SetProfile(folder, ""));
  EXPECT_EQ(model.EffectiveProfile(page), "");
}

TEST(BookmarkModelTest, GeneralItemsAreVisibleEverywhere) {
  BookmarkModel model;
  BookmarkId page = AddUrl(model, kBar, "https://a.com/");
  BookmarkId linked = AddUrl(model, kBar, "https://b.com/", "work");
  EXPECT_TRUE(model.IsVisibleIn(page, "work"));
  EXPECT_TRUE(model.IsVisibleIn(page, "home"));
  EXPECT_TRUE(model.IsVisibleIn(linked, "work"));
  EXPECT_FALSE(model.IsVisibleIn(linked, "home"));
}

TEST(BookmarkModelTest, RemoveAndRestore) {
  BookmarkModel model;
  BookmarkId a = AddUrl(model, kBar, "https://a.com/");
  BookmarkId folder = AddDir(model, kBar, "F");
  BookmarkId inside = AddUrl(model, folder, "https://in.com/");
  BookmarkId c = AddUrl(model, kBar, "https://c.com/");
  std::optional<BookmarkRemoval> removal = model.Remove(folder);
  ASSERT_TRUE(removal);
  EXPECT_EQ(removal->items.size(), 2U);
  EXPECT_EQ(model.Find(inside), nullptr);
  EXPECT_EQ(model.Find(kBar)->children, (std::vector<BookmarkId>{a, c}));
  EXPECT_EQ(model.Restore(*removal), folder);
  EXPECT_EQ(model.Find(kBar)->children,
            (std::vector<BookmarkId>{a, folder, c}));
  ASSERT_NE(model.Find(inside), nullptr);
  EXPECT_EQ(model.Find(inside)->parent, folder);
  EXPECT_EQ(model.Restore(*removal), 0) << "already back";
}

TEST(BookmarkModelTest, RestoreIntoAGoneFolderGoesToOtherBookmarks) {
  BookmarkModel model;
  BookmarkId folder = AddDir(model, kBar, "F");
  BookmarkId page = AddUrl(model, folder, "https://a.com/");
  std::optional<BookmarkRemoval> page_removal = model.Remove(page);
  model.Remove(folder);
  ASSERT_TRUE(page_removal);
  EXPECT_EQ(model.Restore(*page_removal), page);
  EXPECT_EQ(model.Find(page)->parent, kOther);
  // New ids never reuse restored ones.
  EXPECT_GT(AddUrl(model, kBar, "https://b.com/"), page);
}

TEST(BookmarkModelTest, FindsBookmarksByUrlInTheProfile) {
  BookmarkModel model;
  BookmarkId general = AddUrl(model, kBar, "https://a.com/");
  BookmarkId work = AddUrl(model, kOther, "https://a.com/", "work");
  EXPECT_EQ(model.BookmarksWithUrl("https://a.com/", "work"),
            (std::vector<BookmarkId>{general, work}));
  EXPECT_EQ(model.BookmarksWithUrl("https://a.com/", "home"),
            (std::vector<BookmarkId>{general}));
  EXPECT_TRUE(model.BookmarksWithUrl("https://b.com/", "home").empty());
}

TEST(BookmarkModelTest, SearchMatchesEveryWordInNameOrUrl) {
  BookmarkModel model;
  Result<BookmarkId> jira = model.AddBookmark(
      kBar, kEnd, "Sprint board", "https://jira.example/board", "work", 1);
  Result<BookmarkId> wiki = model.AddBookmark(kOther, kEnd, "Team wiki",
                                              "https://wiki.example/", "", 1);
  ASSERT_TRUE(jira && wiki);
  EXPECT_EQ(model.Search("JIRA sprint", "work", 10),
            (std::vector<BookmarkId>{jira.value()}));
  EXPECT_TRUE(model.Search("jira", "home", 10).empty());
  EXPECT_EQ(model.Search("example", "", 10),
            (std::vector<BookmarkId>{jira.value(), wiki.value()}));
  EXPECT_EQ(model.Search("example", "", 1).size(), 1U);
  EXPECT_TRUE(model.Search("  ", "", 10).empty());
}

TEST(BookmarkModelTest, FoldersInTreeOrder) {
  BookmarkModel model;
  BookmarkId a = AddDir(model, kBar, "A");
  BookmarkId a1 = AddDir(model, a, "A1");
  BookmarkId w = AddDir(model, kOther, "W", "work");
  std::vector<BookmarkFolderEntry> all = model.Folders("");
  ASSERT_EQ(all.size(), 5U);
  EXPECT_EQ(all[0].id, kBar);
  EXPECT_EQ(all[1].id, a);
  EXPECT_EQ(all[2].id, a1);
  EXPECT_EQ(all[2].depth, 2);
  EXPECT_EQ(all[3].id, kOther);
  EXPECT_EQ(all[4].id, w);
  EXPECT_EQ(model.Folders("home").size(), 4U);
}

TEST(BookmarkModelTest, DeletingAProfileMakesItsItemsGeneral) {
  BookmarkModel model;
  BookmarkId folder = AddDir(model, kBar, "W", "work");
  AddUrl(model, folder, "https://a.com/");
  BookmarkId page = AddUrl(model, kOther, "https://b.com/", "work");
  AddUrl(model, kOther, "https://c.com/", "home");
  EXPECT_EQ(model.CountLinkedTo("work"), 2);
  EXPECT_EQ(model.UnlinkProfile("work"), 2);
  EXPECT_EQ(model.EffectiveProfile(folder), "");
  EXPECT_EQ(model.EffectiveProfile(page), "");
  EXPECT_EQ(model.CountLinkedTo("home"), 1);
  EXPECT_EQ(model.CountLinkedTo(""), 0);
}

TEST(BookmarkModelTest, RevisionChangesWithEveryChange) {
  BookmarkModel model;
  std::uint64_t start = model.revision();
  BookmarkId page = AddUrl(model, kBar, "https://a.com/");
  EXPECT_GT(model.revision(), start);
  std::uint64_t added = model.revision();
  EXPECT_FALSE(model.Rename(page, "A"));
  EXPECT_GT(model.revision(), added);
}

TEST(BookmarkModelTest, JsonRoundTrip) {
  BookmarkModel model;
  BookmarkId folder = AddDir(model, kBar, "Work", "work");
  BookmarkId page = AddUrl(model, folder, "https://jira.example/");
  BookmarkId other = AddUrl(model, kOther, "https://b.com/", "home");
  Result<BookmarkModel> back = BookmarkModel::FromJson(model.ToJson());
  ASSERT_TRUE(back) << back.error().message;
  const BookmarkModel& copy = back.value();
  EXPECT_EQ(copy.Find(kBar)->children, (std::vector<BookmarkId>{folder}));
  EXPECT_EQ(copy.Find(folder)->profile, "work");
  EXPECT_EQ(copy.Find(page)->url, "https://jira.example/");
  EXPECT_EQ(copy.Find(page)->added, 100);
  EXPECT_EQ(copy.Find(other)->profile, "home");
  EXPECT_EQ(WriteJsonDocument(copy.ToJson()),
            WriteJsonDocument(model.ToJson()));
}

TEST(BookmarkModelTest, ReadingRepairsBadItems) {
  Result<JsonValue> json = ParseJsonDocument(R"({
    "version": 1, "next_id": 3,
    "roots": {
      "bookmark_bar": {"children": [
        {"id": 5, "type": "url", "name": "A", "url": "https://a.com/"},
        {"id": 5, "type": "url", "name": "Dup", "url": "https://dup.com/"},
        {"type": "url", "name": "NoId", "url": "https://noid.com/"},
        {"id": 6, "type": "url", "name": "NoUrl"},
        {"id": 7, "type": "weird", "name": "X"},
        {"id": 8, "type": "folder", "name": "W", "profile": "work",
         "children": [{"id": 9, "type": "url", "name": "In",
                       "url": "https://in.com/", "profile": "home"}]},
        {"id": 10, "type": "url", "name": "Bad profile",
         "url": "https://p.com/", "profile": "NOT VALID"}
      ]},
      "other": {"children": []}
    }})");
  ASSERT_TRUE(json) << json.error().message;
  Result<BookmarkModel> model = BookmarkModel::FromJson(json.value());
  ASSERT_TRUE(model) << model.error().message;
  const std::vector<BookmarkId>& bar = model.value().Find(kBar)->children;
  ASSERT_EQ(bar.size(), 5U);
  EXPECT_EQ(bar[0], 5);
  EXPECT_EQ(model.value().Find(bar[1])->url, "https://dup.com/");
  EXPECT_GT(bar[1], 10) << "a repeated id gets a new one";
  EXPECT_GT(bar[2], 10);
  EXPECT_NE(bar[1], bar[2]);
  EXPECT_EQ(model.value().Find(9)->profile, "") << "follows its folder";
  EXPECT_EQ(model.value().Find(10)->profile, "");
  EXPECT_FALSE(BookmarkModel::FromJson(JsonValue::Array()));
  EXPECT_FALSE(BookmarkModel::FromJson(
      ParseJsonDocument(R"({"version": 99, "roots": {}})").value()));
}

}  // namespace
}  // namespace brouk
