#ifndef BROUK_CORE_DEVTOOLS_TOOLS_H_
#define BROUK_CORE_DEVTOOLS_TOOLS_H_

#include <span>
#include <string_view>

namespace brouk {

// Our own developer tools (006: site data, tokens, requests) are the
// internal page brouk://tools/, shown in a tab's dock. They work through the
// Chrome DevTools Protocol of that tab only (so only with its profile's
// data), and only with the methods listed here.
bool IsAllowedToolsProtocolMethod(std::string_view method);

// Web links the tools page may open in a tab: the OWASP references of the
// security check (029). Anything else stays blocked, as on other internal
// pages.
bool ToolsPageOpensLink(std::string_view url);

// One of the dev tools' Utilities (027), for the menu and the command
// palette; the same ids and titles as the tools page (web/tools/).
struct DevToolsUtility {
  std::string_view id;
  std::string_view title;
};

// In the order of the Utilities tab.
std::span<const DevToolsUtility> DevToolsUtilities();

}  // namespace brouk

#endif  // BROUK_CORE_DEVTOOLS_TOOLS_H_
