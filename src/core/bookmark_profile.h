#ifndef BROUK_CORE_BOOKMARK_PROFILE_H_
#define BROUK_CORE_BOOKMARK_PROFILE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "core/bookmark_model.h"
#include "core/profile_id.h"
#include "core/yaml_value.h"

namespace brouk {

// A profile's bookmarks in its export file (011, extending 005): every
// item linked to the profile, with everything inside it and the names of
// the folders above it:
//
//   bookmarks:
//     - root: bar            (bar or other)
//       path: ["Work"]       (folder names below the root)
//       item: {type: folder, name: "Jira", children: [...]}
//
// Profile links are not written: on import everything is linked to the
// new profile.
YamlNode ExportProfileBookmarks(const BookmarkModel& model,
                                const ProfileId& profile);

struct ProfileBookmarksImport {
  int items = 0;                      // Bookmarks and folders created.
  std::vector<std::string> problems;  // Entries that could not be read.
};

// Recreates the bookmarks of an export (`node` is its "bookmarks" section)
// under their folder paths, linked to `profile`. Missing folders on the way
// are created as general folders; existing general folders of the same
// name are reused. Invalid entries are skipped and listed.
ProfileBookmarksImport ImportProfileBookmarks(BookmarkModel& model,
                                              const YamlNode& node,
                                              const ProfileId& profile,
                                              std::int64_t now);

}  // namespace brouk

#endif  // BROUK_CORE_BOOKMARK_PROFILE_H_
