#include "platform/win/wide_string.h"

#include <windows.h>

#include <climits>

namespace brouk::win {

std::wstring Utf8ToWide(std::string_view text) {
  if (text.empty() || text.size() > INT_MAX) {
    return {};
  }
  const int input_size = static_cast<int>(text.size());
  const int size =
      MultiByteToWideChar(CP_UTF8, 0, text.data(), input_size, nullptr, 0);
  if (size <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), input_size, wide.data(), size);
  return wide;
}

std::string WideToUtf8(std::wstring_view text) {
  if (text.empty() || text.size() > INT_MAX) {
    return {};
  }
  const int input_size = static_cast<int>(text.size());
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), input_size,
                                       nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string utf8(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), input_size, utf8.data(), size,
                      nullptr, nullptr);
  return utf8;
}

}  // namespace brouk::win
