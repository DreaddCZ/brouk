#include "core/bookmark_file.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "core/json.h"
#include "core/utf8_path.h"

namespace brouk {

namespace {

// Files larger than this are not bookmark files of ours.
constexpr std::uintmax_t kMaxFileBytes = std::uintmax_t{256} * 1024 * 1024;

std::filesystem::path WithSuffix(const std::filesystem::path& path,
                                 const char* suffix) {
  std::filesystem::path result = path;
  result += suffix;
  return result;
}

Result<BookmarkModel> Read(const std::filesystem::path& path) {
  std::error_code error;
  std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error) {
    return Error{"cannot read " + PathToUtf8(path) + ": " + error.message()};
  }
  if (size > kMaxFileBytes) {
    return Error{PathToUtf8(path) + " is too large"};
  }
  std::ifstream file(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  if (!file && !file.eof()) {
    return Error{"cannot read " + PathToUtf8(path)};
  }
  Result<JsonValue> json = ParseJsonDocument(text);
  if (!json) {
    return Error{PathToUtf8(path) + ": " + json.error().message};
  }
  Result<BookmarkModel> model = BookmarkModel::FromJson(json.value());
  if (!model) {
    return Error{PathToUtf8(path) + ": " + model.error().message};
  }
  return model;
}

}  // namespace

LoadedBookmarks LoadBookmarks(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::path backup = WithSuffix(path, ".bak");
  if (!std::filesystem::exists(path, error)) {
    // A crash between keeping the backup and renaming the new file leaves
    // only the backup.
    if (!std::filesystem::exists(backup, error)) {
      return {.model = BookmarkModel(), .problem = std::nullopt};
    }
    Result<BookmarkModel> from_backup = Read(backup);
    if (from_backup) {
      return {.model = std::move(from_backup).value(),
              .problem = PathToUtf8(path) + " is missing; using the backup"};
    }
    return {.model = BookmarkModel(), .problem = from_backup.error().message};
  }
  Result<BookmarkModel> model = Read(path);
  if (model) {
    return {.model = std::move(model).value(), .problem = std::nullopt};
  }
  std::string problem = model.error().message;
  Result<BookmarkModel> from_backup = Read(backup);
  if (from_backup) {
    return {.model = std::move(from_backup).value(),
            .problem = problem + "; using the backup"};
  }
  return {.model = BookmarkModel(), .problem = problem};
}

std::optional<Error> SaveBookmarks(const std::filesystem::path& path,
                                   const BookmarkModel& model) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return Error{"cannot create " + PathToUtf8(path.parent_path()) + ": " +
                 error.message()};
  }
  std::filesystem::path temp = WithSuffix(path, ".tmp");
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file << WriteJsonDocument(model.ToJson());
    file.close();
    if (!file) {
      std::filesystem::remove(temp, error);
      return Error{"cannot write " + PathToUtf8(temp)};
    }
  }
  std::filesystem::permissions(
      temp,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      error);
  // Keep the previous version, then move the new one into place.
  if (std::filesystem::exists(path, error)) {
    std::filesystem::path backup = WithSuffix(path, ".bak");
    std::filesystem::rename(path, backup, error);
    if (error) {
      std::error_code ignored;
      std::filesystem::remove(temp, ignored);
      return Error{"cannot keep " + PathToUtf8(backup) + ": " +
                   error.message()};
    }
  }
  std::filesystem::rename(temp, path, error);
  if (error) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return Error{"cannot replace " + PathToUtf8(path) + ": " + error.message()};
  }
  return std::nullopt;
}

}  // namespace brouk
