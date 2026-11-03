#ifndef BROUK_CORE_LOG_FILE_H_
#define BROUK_CORE_LOG_FILE_H_

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "core/result.h"

namespace brouk {

// A log file that never grows without bound (025): `<name>.log` takes new
// lines; when the next line would make it bigger than `max_bytes`, it
// becomes `<name>.1.log`, the older ones move up one number, and the
// oldest beyond `max_files` is deleted. Not thread-safe: the logger
// (log.h) serialises writes.
class RotatingLogFile {
 public:
  struct Limits {
    std::uintmax_t max_bytes = std::uintmax_t{5} * 1024 * 1024;
    int max_files = 3;  // Including the current one.
  };

  // Opens (or creates) `directory`/`name`.log for appending, creating the
  // directory if needed.
  static Result<RotatingLogFile> Open(const std::filesystem::path& directory,
                                      std::string_view name,
                                      Limits limits);
  static Result<RotatingLogFile> Open(const std::filesystem::path& directory,
                                      std::string_view name) {
    return Open(directory, name, Limits());
  }

  RotatingLogFile(RotatingLogFile&&) = default;
  RotatingLogFile& operator=(RotatingLogFile&&) = default;

  // Appends `line` and a line break, then flushes, so a crash right after
  // does not lose it.
  void Write(std::string_view line);

  std::filesystem::path current_path() const { return PathOf(0); }

 private:
  RotatingLogFile(std::filesystem::path directory,
                  std::string name,
                  Limits limits);
  // `<name>.log` for 0, `<name>.<n>.log` for older files.
  std::filesystem::path PathOf(int index) const;
  void Rotate();
  bool OpenCurrent();

  std::filesystem::path directory_;
  std::string name_;
  Limits limits_;
  std::ofstream file_;
  std::uintmax_t size_ = 0;
};

}  // namespace brouk

#endif  // BROUK_CORE_LOG_FILE_H_
