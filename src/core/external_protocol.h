#ifndef BROUK_CORE_EXTERNAL_PROTOCOL_H_
#define BROUK_CORE_EXTERNAL_PROTOCOL_H_

#include <string>
#include <string_view>

namespace brouk {

// What to do with a navigation to a URL scheme (012, item 14).
enum class SchemeHandling {
  kBrowser,   // Loaded by the browser (http, https, data, about, ...).
  kBlocked,   // Never handed to another app (Chrome's deny list).
  kExternal,  // Opened in another app after asking (mailto, msteams, ...).
};

// Classifies the scheme of `url` (case-insensitive). URLs without a valid
// scheme are left to the browser.
SchemeHandling ClassifyScheme(std::string_view url);

// The lower-case scheme of `url` ("mailto"), or empty.
std::string SchemeOf(std::string_view url);

// Key for remembering "always allow <origin> to open <scheme> links":
// "<origin> <scheme>". Origins never contain spaces.
std::string ExternalProtocolAllowKey(std::string_view origin,
                                     std::string_view scheme);

}  // namespace brouk

#endif  // BROUK_CORE_EXTERNAL_PROTOCOL_H_
