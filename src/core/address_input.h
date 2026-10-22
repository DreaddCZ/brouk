#ifndef BROUK_CORE_ADDRESS_INPUT_H_
#define BROUK_CORE_ADDRESS_INPUT_H_

#include <string>
#include <string_view>

namespace brouk {

// Default search engine (003): Google. %s is replaced by the encoded query.
inline constexpr std::string_view kDefaultSearchTemplate =
    "https://www.google.com/search?q=%s";

// What the user typed into the address bar, turned into something loadable.
struct AddressInputResult {
  enum class Kind {
    kEmpty,   // Nothing to load (only whitespace).
    kUrl,     // `url` is the URL to navigate to.
    kSearch,  // `url` is a search URL for the typed text.
  };
  Kind kind = Kind::kEmpty;
  std::string url;
};

// Decides whether address bar text is a URL or a search, following Chrome's
// rules closely enough for daily use:
// - text with a known scheme ("https:", "about:", "file:", ...) or "://" is a
//   URL as typed;
// - "host[:port][/path]" where host is localhost, an IP address or a domain
//   with an alphabetic top-level domain is a URL; https is added for domains,
//   http for localhost, IP addresses and single-label hosts (as Chrome does);
// - everything else (spaces, single words, "user@host") is a search.
// `search_template` contains "%s", replaced by the form-encoded query.
AddressInputResult ClassifyAddressInput(
    std::string_view input,
    std::string_view search_template = kDefaultSearchTemplate);

// The search URL for `query`: "%s" in `search_template` replaced by the
// form-encoded query (used for "Search Google for …" too).
std::string BuildSearchUrl(
    std::string_view query,
    std::string_view search_template = kDefaultSearchTemplate);

// Form-encodes text for a query string: unreserved characters stay, spaces
// become '+', everything else becomes %XX of its UTF-8 bytes.
std::string EncodeQueryComponent(std::string_view text);

}  // namespace brouk

#endif  // BROUK_CORE_ADDRESS_INPUT_H_
