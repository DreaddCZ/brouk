#ifndef BROUK_CORE_ADBLOCK_STORE_H_
#define BROUK_CORE_ADBLOCK_STORE_H_

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/adblock_engine.h"
#include "core/adblock_lists.h"
#include "core/result.h"

namespace brouk {

// How a downloaded list stands (007: a failed update keeps the previous
// version and shows the error).
struct FilterListState {
  std::int64_t updated = 0;  // Last successful download, seconds since 1970.
  std::optional<int> expires_hours;
  std::string error;           // Of the last attempt; empty if it worked.
  std::int64_t attempted = 0;  // Last attempt, successful or not.
};

// An engine and what its list numbers mean (for "which list blocked it").
struct BuiltEngine {
  std::unique_ptr<AdblockEngine> engine;
  // Index i is the id of list i in matches; "my-filters" for the user's.
  std::vector<std::string> list_ids;
};

inline constexpr std::string_view kMyFiltersId = "my-filters";
// Largest list accepted (the biggest default lists are about 4 MB).
inline constexpr std::size_t kMaxFilterListBytes =
    std::size_t{32} * 1024 * 1024;

// The ad blocker's folder (adblock/ in the app data folder): downloaded
// lists, their state, My filters, resources and cached engines. Not
// thread-safe: one thread (the ad blocker's) uses it.
class AdblockStore {
 public:
  explicit AdblockStore(std::filesystem::path dir);

  // A list's text as last downloaded; nullopt if never.
  std::optional<std::string> ReadList(std::string_view id) const;
  // Checks and saves a downloaded list (written atomically). A text that
  // is not a filter list (an HTML page from a captive portal, too big)
  // is refused: the previous version stays and the error is recorded.
  std::optional<Error> SaveDownload(std::string_view id,
                                    std::string_view text,
                                    std::int64_t now);
  void SaveError(std::string_view id, std::string error, std::int64_t now);
  const std::map<std::string, FilterListState, std::less<>>& states() const {
    return states_;
  }
  // Due for download: never downloaded, or older than its interval. A
  // failed attempt waits an hour before the next one.
  bool IsDue(std::string_view id, std::int64_t now) const;
  // Drops the files of lists that are no longer used.
  void RemoveUnused(std::span<const std::string> used_ids);

  std::string ReadMyFilters() const;
  std::optional<Error> SaveMyFilters(std::string_view text);

  // Redirect and scriptlet resources downloaded with the lists; nullopt
  // until the first download (the bundled copy is used then).
  std::optional<std::string> ReadResources() const;
  std::optional<Error> SaveResources(std::string_view json, std::int64_t now);

  // An engine from `lists` (those downloaded) and My filters, from the
  // cache when it was built from the same texts before. Null when no
  // list has been downloaded yet and My filters is empty.
  BuiltEngine LoadOrBuildEngine(std::span<const FilterListDef> lists,
                                std::string_view resources_json);

 private:
  std::filesystem::path ListPath(std::string_view id) const;
  void LoadStates();
  void SaveStates() const;

  std::filesystem::path dir_;
  std::map<std::string, FilterListState, std::less<>> states_;
};

// Whether `text` looks like a filter list rather than, say, a web page.
bool LooksLikeFilterList(std::string_view text);

}  // namespace brouk

#endif  // BROUK_CORE_ADBLOCK_STORE_H_
