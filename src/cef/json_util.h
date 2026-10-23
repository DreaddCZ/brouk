#ifndef BROUK_CEF_JSON_UTIL_H_
#define BROUK_CEF_JSON_UTIL_H_

#include <string_view>

#include "include/cef_parser.h"

namespace brouk {

// CefParseJSON, but null for empty text (CefParseJSON treats empty input as
// a programming error and stops the process).
inline CefRefPtr<CefValue> ParseJson(std::string_view text) {
  if (text.empty()) {
    return nullptr;
  }
  return CefParseJSON(text.data(), text.size(), JSON_PARSER_RFC);
}

}  // namespace brouk

#endif  // BROUK_CEF_JSON_UTIL_H_
