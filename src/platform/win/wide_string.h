#ifndef BROUK_PLATFORM_WIN_WIDE_STRING_H_
#define BROUK_PLATFORM_WIN_WIDE_STRING_H_

#include <string>
#include <string_view>

namespace brouk::win {

// Win32 text is UTF-16; the core and CEF use UTF-8. Invalid input is replaced
// (U+FFFD), never rejected.
std::wstring Utf8ToWide(std::string_view text);
std::string WideToUtf8(std::wstring_view text);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_WIDE_STRING_H_
