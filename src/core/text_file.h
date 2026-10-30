#ifndef BROUK_CORE_TEXT_FILE_H_
#define BROUK_CORE_TEXT_FILE_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/result.h"

namespace brouk {

// Reads a whole file the user chose (an import, 005). Files bigger than
// `max_bytes` are refused before reading: such a file is not what the
// caller expects, and reading it could use a lot of memory.
Result<std::string> ReadTextFile(const std::filesystem::path& path,
                                 std::uintmax_t max_bytes);

// Writes `text` to a temporary file next to `path`, then renames it over
// `path`: a crash never leaves a half-written file behind.
std::optional<Error> WriteTextFileAtomically(const std::filesystem::path& path,
                                             std::string_view text);

}  // namespace brouk

#endif  // BROUK_CORE_TEXT_FILE_H_
