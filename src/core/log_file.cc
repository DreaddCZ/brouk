#include "core/log_file.h"

#include <system_error>
#include <utility>

#include "core/utf8_path.h"

namespace brouk {

RotatingLogFile::RotatingLogFile(std::filesystem::path directory,
                                 std::string name,
                                 Limits limits)
    : directory_(std::move(directory)),
      name_(std::move(name)),
      limits_(limits) {}

Result<RotatingLogFile> RotatingLogFile::Open(
    const std::filesystem::path& directory,
    std::string_view name,
    Limits limits) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    return Error{"Cannot create the log folder " + PathToUtf8(directory) +
                 ": " + error.message()};
  }
  if (limits.max_files < 1) {
    limits.max_files = 1;
  }
  RotatingLogFile log(directory, std::string(name), limits);
  if (!log.OpenCurrent()) {
    return Error{"Cannot open the log file " + PathToUtf8(log.PathOf(0))};
  }
  return log;
}

std::filesystem::path RotatingLogFile::PathOf(int index) const {
  if (index == 0) {
    return directory_ / PathFromUtf8(name_ + ".log");
  }
  return directory_ /
         PathFromUtf8(name_ + "." + std::to_string(index) + ".log");
}

bool RotatingLogFile::OpenCurrent() {
  const std::filesystem::path path = PathOf(0);
  file_.open(path, std::ios::binary | std::ios::app);
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  size_ = error ? 0 : size;
  return file_.is_open();
}

void RotatingLogFile::Rotate() {
  file_.close();
  std::error_code ignored;
  std::filesystem::remove(PathOf(limits_.max_files - 1), ignored);
  for (int index = limits_.max_files - 2; index >= 0; --index) {
    std::filesystem::rename(PathOf(index), PathOf(index + 1), ignored);
  }
  // With one file, the old lines are dropped.
  std::filesystem::remove(PathOf(0), ignored);
  OpenCurrent();
}

void RotatingLogFile::Write(std::string_view line) {
  const std::uintmax_t bytes = line.size() + 1;
  if (size_ > 0 && size_ + bytes > limits_.max_bytes) {
    Rotate();
  }
  if (!file_.is_open()) {
    return;
  }
  file_.write(line.data(), static_cast<std::streamsize>(line.size()));
  file_.put('\n');
  file_.flush();
  size_ += bytes;
}

}  // namespace brouk
