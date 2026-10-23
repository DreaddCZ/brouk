#ifndef BROUK_CORE_BOOKMARK_HTML_H_
#define BROUK_CORE_BOOKMARK_HTML_H_

#include <cstdint>
#include <string>
#include <string_view>

#include "core/bookmark_model.h"

namespace brouk {

// The standard ("Netscape") HTML bookmark file every browser exports and
// imports (011). Profile links are not part of the format.

// All bookmarks; the bar's folder is marked as the toolbar folder, as
// Chrome does.
std::string ExportBookmarksHtml(const BookmarkModel& model);

struct HtmlImportResult {
  BookmarkId folder = 0;  // The "Imported" folder; 0 if nothing was found.
  int bookmarks = 0;
  int folders = 0;
};

// Adds the bookmarks of `html` as general items in a new folder "Imported"
// at the end of "Other bookmarks", keeping their folders. The file is
// untrusted: only http, https, ftp and file addresses are taken, folders
// deeper than 100 levels are flattened, and at most 100,000 items are read.
HtmlImportResult ImportBookmarksHtml(BookmarkModel& model,
                                     std::string_view html,
                                     std::int64_t now);

}  // namespace brouk

#endif  // BROUK_CORE_BOOKMARK_HTML_H_
