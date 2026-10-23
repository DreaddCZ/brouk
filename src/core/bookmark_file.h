#ifndef BROUK_CORE_BOOKMARK_FILE_H_
#define BROUK_CORE_BOOKMARK_FILE_H_

#include <filesystem>
#include <optional>
#include <string>

#include "core/bookmark_model.h"
#include "core/result.h"

namespace brouk {

// bookmarks.json in the app data folder (011), written as Chrome writes its
// Bookmarks file: the previous version is kept as bookmarks.json.bak and the
// new one is written to a temporary file and renamed into place, so a crash
// while saving never loses the bookmarks.

struct LoadedBookmarks {
  BookmarkModel model;
  // Set when the file could not be read and the backup (or an empty tree)
  // was used instead: what went wrong, for the log.
  std::optional<std::string> problem;
};

// A missing file gives an empty tree without a problem.
LoadedBookmarks LoadBookmarks(const std::filesystem::path& path);

std::optional<Error> SaveBookmarks(const std::filesystem::path& path,
                                   const BookmarkModel& model);

}  // namespace brouk

#endif  // BROUK_CORE_BOOKMARK_FILE_H_
