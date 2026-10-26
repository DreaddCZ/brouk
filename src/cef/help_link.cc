#include "cef/help_link.h"

#include <string>
#include <utility>

#include "cef/json_util.h"
#include "core/help.h"
#include "core/internal_pages.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

HelpLinkOpeners& Openers() {
  static HelpLinkOpeners openers;
  return openers;
}

}  // namespace

void SetHelpLinkOpeners(HelpLinkOpeners openers) {
  CEF_REQUIRE_UI_THREAD();
  Openers() = std::move(openers);
}

bool OpenHelpWebLink(const std::string& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!url.starts_with("https://") && !url.starts_with("http://")) {
    return false;
  }
  if (Openers().open_web_link) {
    Openers().open_web_link(url);
  }
  return true;
}

bool HelpOpenHandler::OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                              CefRefPtr<CefFrame> frame,
                              int64_t /*query_id*/,
                              const CefString& request,
                              bool persistent,
                              CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  std::string text = request.ToString();
  // Cheap test first: every page's messages pass through here.
  if (persistent || text.size() > 512 ||
      text.find("\"help.open\"") == std::string::npos || !frame ||
      !InternalPageOf(frame->GetURL().ToString())) {
    return false;
  }
  CefRefPtr<CefValue> parsed = ParseJson(text);
  CefRefPtr<CefDictionaryValue> message =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  if (!message || message->GetString("type").ToString() != "help.open") {
    return false;
  }
  std::string anchor = message->GetString("anchor").ToString();
  if (!IsHelpAnchor(anchor)) {
    callback->Failure(400, "Invalid request.");
    return true;
  }
  if (Openers().open_help) {
    Openers().open_help(anchor);
  }
  callback->Success("");
  return true;
}

}  // namespace brouk
