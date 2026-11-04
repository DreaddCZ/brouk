#ifndef BROUK_CORE_HISTORY_PROFILE_H_
#define BROUK_CORE_HISTORY_PROFILE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/history_store.h"
#include "core/yaml_value.h"

namespace brouk {

// How much history goes into a profile export (022): the visits of the
// last `count` days, or every visit of the last `count` visited pages.
struct HistoryExportLimit {
  enum class Kind { kDays, kPages };
  Kind kind = Kind::kDays;
  int count = 30;
};

// The most a limit may ask for, so a file stays a reasonable size.
inline constexpr int kHistoryExportMaxDays = 3650;
inline constexpr int kHistoryExportMaxPages = 20000;

// A profile's history in its export file, newest page first:
//
//   history:
//     - url: "https://example.com/"
//       title: "Example"
//       visits:              newest first; time in seconds since 1970 (UTC)
//         - {time: 1730000000, how: typed}
//
// `how` is link, typed, bookmark, reload or other. Not exported: the
// downloads list, favicons and the Clean up keep list.
YamlNode ExportProfileHistory(HistoryStore& store,
                              const HistoryExportLimit& limit,
                              std::int64_t now);

struct ProfileHistoryImport {
  int pages = 0;
  int visits = 0;
  std::vector<std::string> problems;  // Entries that could not be read.
};

// The retention limit (013) for an import: visits before the returned time
// are not kept. `keep` is the setting history.keep ("forever" or days).
std::optional<std::int64_t> HistoryKeepSince(std::string_view keep,
                                             std::int64_t now);

// Adds the visits of an export (`node` is its "history" section) to
// `store`, the new profile's history. Visits before `keep_since` (the
// retention limit, 013) are left out; pages that are never recorded
// (brouk://, data:…) and invalid entries are skipped and listed.
ProfileHistoryImport ImportProfileHistory(
    HistoryStore& store,
    const YamlNode& node,
    std::optional<std::int64_t> keep_since);

}  // namespace brouk

#endif  // BROUK_CORE_HISTORY_PROFILE_H_
