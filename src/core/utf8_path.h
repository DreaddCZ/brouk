#ifndef BROUK_CORE_UTF8_PATH_H_
#define BROUK_CORE_UTF8_PATH_H_

#include <filesystem>
#include <string>
#include <string_view>

namespace brouk {

// Paths as UTF-8 text and back (024). On Windows, std::filesystem::path's
// narrow strings use the system's code page, not UTF-8, and converting a
// character that code page lacks ends the program (Brouk has no
// exceptions): a user folder such as "C:\Users\Jiří" broke SQLite, which
// takes UTF-8 file names. Everything that hands a path to a UTF-8 API
// (SQLite, a program's environment, messages) or reads one from UTF-8 text
// (settings) uses these. On macOS and Linux they change nothing.
std::string PathToUtf8(const std::filesystem::path& path);
// An empty path for text that is not valid UTF-8.
std::filesystem::path PathFromUtf8(std::string_view text);

}  // namespace brouk

#endif  // BROUK_CORE_UTF8_PATH_H_
