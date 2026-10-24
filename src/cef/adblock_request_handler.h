#ifndef BROUK_CEF_ADBLOCK_REQUEST_HANDLER_H_
#define BROUK_CEF_ADBLOCK_REQUEST_HANDLER_H_

#include <optional>
#include <string>
#include <string_view>

#include "cef/adblock_service.h"
#include "core/profile_id.h"
#include "include/cef_request.h"
#include "include/cef_resource_request_handler.h"

namespace brouk {

// One request as the ad blocker saw it (007), for the shield's count, the
// popup's third-party domains and the blocked requests list.
struct AdblockRequestInfo {
  std::string url;
  std::string host;
  std::string type;  // The engine's request type ("script", "image"…).
  bool main_frame = false;
  bool third_party = false;
  bool blocked = false;
  bool excepted = false;  // Matched, but allowed (exception, protected host).
  std::string rule;       // The rule that blocked or excepted it.
  std::string list_id;    // Its list ("my-filters" for the user's).
};

// The engine's name for a CEF resource type.
std::string_view AdblockRequestType(cef_resource_type_t type);

// Decides a request on the IO thread. Returns null when it goes through
// unchanged (the decision is still reported through `info`), else a handler
// that cancels it, serves a redirect resource, or loads it without its
// tracking parameters. `info->url` is empty when the ad blocker does not
// apply (not http(s), blocking off, an allowlisted site, no engine yet).
CefRefPtr<CefResourceRequestHandler> CheckAdblockRequest(
    const AdblockService& service,
    const ProfileId& profile,
    const std::string& url,
    const std::string& page_url,
    cef_resource_type_t type,
    AdblockRequestInfo* info);

// Cosmetic filtering for a frame (007): what its page script needs (JSON:
// hide, procedural, exceptions, generichide) and the scriptlets the
// renderer runs. nullopt when blocking does not apply to the page.
struct AdblockCosmeticAnswer {
  std::string json;
  std::string scriptlets;
};
std::optional<AdblockCosmeticAnswer> AdblockCosmeticFor(
    const AdblockService& service,
    const ProfileId& profile,
    const std::string& frame_url,
    const std::string& page_url);
// Generic hiding selectors for the classes and ids a page script reported
// (untrusted JSON): JSON {"generic": [...]}, or nullopt.
std::optional<std::string> AdblockGenericSelectorsFor(
    const AdblockService& service,
    const ProfileId& profile,
    const std::string& page_url,
    const std::string& request_json);

}  // namespace brouk

#endif  // BROUK_CEF_ADBLOCK_REQUEST_HANDLER_H_
