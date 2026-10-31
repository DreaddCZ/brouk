#include "core/text_file.h"

#include <fstream>
#include <iterator>
#include <system_error>

namespace brouk {

Result<std::string> ReadTextFile(const std::filesystem::path& path,
                                 std::uintmax_t max_bytes) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error) {
    return Error{"The file could not be read."};
  }
  if (size > max_bytes) {
    return Error{"The file is too big."};
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return Error{"The file could not be read."};
  }
  std::string text;
  text.reserve(static_cast<std::size_t>(size));
  text.assign(std::istreambuf_iterator<char>(file),
              std::istreambuf_iterator<char>());
  if (file.bad()) {
    return Error{"The file could not be read."};
  }
  // It may have grown since its size was checked.
  if (text.size() > max_bytes) {
    return Error{"The file is too big."};
  }
  return text;
}

std::optional<Error> WriteTextFileAtomically(const std::filesystem::path& path,
                                             std::string_view text) {
  std::filesystem::path temp = path;
  temp += ".tmp";
  std::error_code error;
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file) {
      std::filesystem::remove(temp, error);
      return Error{"The file could not be saved."};
    }
  }
  std::filesystem::rename(temp, path, error);
  if (error) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return Error{"The file could not be saved: " + error.message()};
  }
  return std::nullopt;
}

}  // namespace brouk
