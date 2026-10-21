#ifndef BROUK_CORE_URL_DISPLAY_H_
#define BROUK_CORE_URL_DISPLAY_H_

#include <string>
#include <string_view>

namespace brouk {

// Shortens a URL for the address bar while it is not being edited, as Chrome
// does by default: hides "http://", "https://", a leading "www." and a lone
// trailing "/". Other schemes are shown in full. Pages without a real address
// (about:blank, the new tab page) show as empty text. While editing, the
// address bar shows the full URL.
std::string FormatUrlForDisplay(std::string_view url);

}  // namespace brouk

#endif  // BROUK_CORE_URL_DISPLAY_H_
