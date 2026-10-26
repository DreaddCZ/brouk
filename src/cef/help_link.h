#ifndef BROUK_CEF_HELP_LINK_H_
#define BROUK_CEF_HELP_LINK_H_

#include <functional>
#include <string>

#include "include/wrapper/cef_message_router.h"

namespace brouk {

// Help (016) is reachable from every internal page: the "?" buttons send
// {"type": "help.open", "anchor": "<section>"} and links on the help page
// open web sites in a tab. The app says once how to do both. UI thread.
struct HelpLinkOpeners {
  // Opens the help window at a section (a checked anchor, 016).
  std::function<void(const std::string& anchor)> open_help;
  // Opens an http(s) address as a tab of the default profile.
  std::function<void(const std::string& url)> open_web_link;
};
void SetHelpLinkOpeners(HelpLinkOpeners openers);

// Opens a web link clicked on the help page; false if it is not one.
bool OpenHelpWebLink(const std::string& url);

// Answers "help.open" from any internal page; other messages are left to
// the page's own handler.
class HelpOpenHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;
};

}  // namespace brouk

#endif  // BROUK_CEF_HELP_LINK_H_
