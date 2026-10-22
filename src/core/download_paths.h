#ifndef BROUK_CORE_DOWNLOAD_PATHS_H_
#define BROUK_CORE_DOWNLOAD_PATHS_H_

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace brouk {

// Makes a file name suggested by a web site safe to use in the downloads
// folder: no directories ("../x", "a/b"), no control characters, no hidden
// dot-files, no empty names, at most 255 bytes. The name comes from the
// server or the page, so it is untrusted.
std::string SanitizeDownloadFileName(std::string_view suggested);

// Chrome's naming for a file that already exists: "name (1).ext",
// "name (2).ext", ... Compound archive extensions stay together
// ("a (1).tar.gz").
std::string NumberedFileName(std::string_view file_name, int number);

// Picks unique paths in the downloads folder (012, item 18): the sanitized
// suggested name, numbered if a file of that name exists or another
// download in progress has reserved it. UI thread only.
class DownloadPathReserver {
 public:
  using ExistsFn = std::function<bool(const std::string& path)>;

  explicit DownloadPathReserver(ExistsFn exists) : exists_(std::move(exists)) {}

  // Returns the reserved full path ("<directory>/<name>").
  std::string Reserve(std::string_view directory, std::string_view suggested);
  // The download finished, failed or was cancelled.
  void Release(const std::string& path);

 private:
  ExistsFn exists_;
  std::set<std::string> reserved_;
};

}  // namespace brouk

#endif  // BROUK_CORE_DOWNLOAD_PATHS_H_
