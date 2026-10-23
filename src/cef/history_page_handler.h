#ifndef BROUK_CEF_HISTORY_PAGE_HANDLER_H_
#define BROUK_CEF_HISTORY_PAGE_HANDLER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "cef/history_service.h"
#include "core/profile_id.h"
#include "core/profile_registry.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// What the history page (brouk://history, 013) needs from the browser; one
// per history tab. UI thread.
class HistoryPageDelegate {
 public:
  virtual ~HistoryPageDelegate() = default;
  virtual std::vector<ProfileInfo> Profiles() = 0;
  // Null before CEF is ready.
  virtual HistoryService* History() = 0;
  // Opens a page of `profile`'s history: in the history tab itself when it
  // is in that profile's group, else (or with `new_tab`) as a new tab of
  // that profile.
  virtual void OpenPage(const ProfileId& profile,
                        const std::string& url,
                        bool new_tab) = 0;
  // The view the page opens with ("site" or "day"), remembered as a setting.
  virtual std::string View() = 0;
  virtual void SetView(const std::string& view) = 0;
};

// Answers the history page's bridge messages (web/history/api.ts). Every
// message is checked: only brouk://history may ask, sizes and types are
// limited, profiles must exist, and only web and file addresses are opened.
// UI thread.
class HistoryPageHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit HistoryPageHandler(std::unique_ptr<HistoryPageDelegate> delegate)
      : delegate_(std::move(delegate)) {}
  HistoryPageHandler(const HistoryPageHandler&) = delete;
  HistoryPageHandler& operator=(const HistoryPageHandler&) = delete;

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;

 private:
  std::unique_ptr<HistoryPageDelegate> delegate_;
};

}  // namespace brouk

#endif  // BROUK_CEF_HISTORY_PAGE_HANDLER_H_
