#include "core/bookmark_html.h"

#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

constexpr BookmarkId kBar = BookmarkModel::kBarId;
constexpr BookmarkId kOther = BookmarkModel::kOtherId;

const BookmarkItem& Child(const BookmarkModel& model,
                          BookmarkId folder,
                          std::size_t index) {
  return *model.Find(model.Find(folder)->children.at(index));
}

// A file as Chrome exports it.
constexpr char kChromeExport[] = R"html(<!DOCTYPE NETSCAPE-Bookmark-file-1>
<!-- This is an automatically generated file. -->
<META HTTP-EQUIV="Content-Type" CONTENT="text/html; charset=UTF-8">
<TITLE>Bookmarks</TITLE>
<H1>Bookmarks</H1>
<DL><p>
    <DT><H3 ADD_DATE="1700000000" LAST_MODIFIED="0" PERSONAL_TOOLBAR_FOLDER="true">Bookmarks bar</H3>
    <DL><p>
        <DT><A HREF="https://jira.example/?a=1&amp;b=2" ADD_DATE="1700000001" ICON="data:image/png;base64,AAA">Jira &amp; board</A>
        <DT><H3 ADD_DATE="1700000002">Docs</H3>
        <DL><p>
            <DT><A HREF="https://docs.example/" ADD_DATE="1700000003">Docs &#269;
              home</A>
        </DL><p>
        <DT><A HREF="javascript:alert(1)">Bookmarklet</A>
    </DL><p>
    <DT><a href='http://other.example/'>Other</a>
</DL><p>
)html";

TEST(BookmarkHtmlTest, ImportsIntoAnImportedFolder) {
  BookmarkModel model;
  HtmlImportResult result = ImportBookmarksHtml(model, kChromeExport, 5);
  EXPECT_EQ(result.bookmarks, 3);
  EXPECT_EQ(result.folders, 2);
  ASSERT_NE(result.folder, 0);
  const BookmarkItem& imported = *model.Find(result.folder);
  EXPECT_EQ(imported.name, "Imported");
  EXPECT_EQ(imported.parent, kOther);
  ASSERT_EQ(imported.children.size(), 2U);
  const BookmarkItem& bar = Child(model, result.folder, 0);
  EXPECT_EQ(bar.name, "Bookmarks bar");
  ASSERT_EQ(bar.children.size(), 2U) << "the bookmarklet is skipped";
  const BookmarkItem& jira = Child(model, bar.id, 0);
  EXPECT_EQ(jira.name, "Jira & board");
  EXPECT_EQ(jira.url, "https://jira.example/?a=1&b=2");
  EXPECT_EQ(jira.added, 1700000001);
  EXPECT_EQ(jira.profile, "");
  const BookmarkItem& docs = Child(model, bar.id, 1);
  EXPECT_TRUE(docs.folder);
  EXPECT_EQ(Child(model, docs.id, 0).name, "Docs \xC4\x8D home");
  const BookmarkItem& other = Child(model, result.folder, 1);
  EXPECT_EQ(other.url, "http://other.example/");
  EXPECT_EQ(other.added, 5) << "no date: the time of the import";
  EXPECT_TRUE(model.Find(kBar)->children.empty());
}

TEST(BookmarkHtmlTest, NothingToImportLeavesNoFolder) {
  BookmarkModel model;
  HtmlImportResult result = ImportBookmarksHtml(model, "<html>hi</html>", 5);
  EXPECT_EQ(result.folder, 0);
  EXPECT_TRUE(model.Find(kOther)->children.empty());
}

TEST(BookmarkHtmlTest, DeepNestingIsFlattened) {
  std::string html;
  for (int i = 0; i < 300; ++i) {
    html += "<DT><H3>F</H3><DL><p>";
  }
  html += "<DT><A HREF=\"https://deep.example/\">Deep</A>";
  for (int i = 0; i < 300; ++i) {
    html += "</DL><p>";
  }
  BookmarkModel model;
  HtmlImportResult result = ImportBookmarksHtml(model, html, 1);
  EXPECT_EQ(result.bookmarks, 1);
  std::vector<BookmarkId> found = model.Search("deep", "", 10);
  ASSERT_EQ(found.size(), 1U);
  int depth = 0;
  for (const BookmarkItem* item = model.Find(found[0]); item != nullptr;
       item = model.Find(item->parent)) {
    ++depth;
  }
  EXPECT_LE(depth, 105);
}

TEST(BookmarkHtmlTest, ExportedFileImportsBack) {
  BookmarkModel model;
  ASSERT_TRUE(model.AddBookmark(kBar, BookmarkModel::kEnd, "A <b> & \"c\"",
                                "https://a.com/?x=1&y=2", "work", 10));
  Result<BookmarkId> folder =
      model.AddFolder(kOther, BookmarkModel::kEnd, "Folder", "", 11);
  ASSERT_TRUE(folder);
  ASSERT_TRUE(model.AddBookmark(folder.value(), BookmarkModel::kEnd, "B",
                                "https://b.com/", "", 12));
  std::string html = ExportBookmarksHtml(model);
  EXPECT_NE(html.find("PERSONAL_TOOLBAR_FOLDER=\"true\""), std::string::npos);
  EXPECT_EQ(html.find("work"), std::string::npos) << "no profile links";

  BookmarkModel back;
  HtmlImportResult result = ImportBookmarksHtml(back, html, 1);
  EXPECT_EQ(result.bookmarks, 2);
  const BookmarkItem& bar = Child(back, result.folder, 0);
  EXPECT_EQ(Child(back, bar.id, 0).name, "A <b> & \"c\"");
  EXPECT_EQ(Child(back, bar.id, 0).url, "https://a.com/?x=1&y=2");
  EXPECT_EQ(Child(back, bar.id, 0).added, 10);
  const BookmarkItem& exported_folder = Child(back, result.folder, 1);
  EXPECT_EQ(exported_folder.name, "Folder");
  EXPECT_EQ(Child(back, exported_folder.id, 0).url, "https://b.com/");
}

}  // namespace
}  // namespace brouk
