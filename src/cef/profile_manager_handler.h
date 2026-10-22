#ifndef BROUK_CEF_PROFILE_MANAGER_HANDLER_H_
#define BROUK_CEF_PROFILE_MANAGER_HANDLER_H_

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/profile_registry.h"
#include "core/result.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// What the profile manager page (brouk://profiles, 005) can ask for.
// Implemented by the platform, which owns the profiles and the windows.
// UI thread.
class ProfileManagerDelegate {
 public:
  struct Summary {
    ProfileInfo info;
    bool is_default = false;
    int open_tabs = 0;
  };
  // deleted is false when the user cancelled the confirmation.
  using DeleteDone =
      std::function<void(bool deleted, std::optional<Error> error)>;

  virtual ~ProfileManagerDelegate() = default;
  virtual std::vector<Summary> Profiles() = 0;
  virtual bool ShowAtStartup() = 0;
  virtual Result<ProfileId> CreateProfile(const std::string& name,
                                          const std::string& color) = 0;
  virtual std::optional<Error> UpdateProfile(const ProfileId& id,
                                             const std::string& name,
                                             const std::string& color) = 0;
  virtual std::optional<Error> SetDefaultProfile(const ProfileId& id) = 0;
  virtual std::optional<Error> SetShowAtStartup(bool show) = 0;
  virtual std::optional<Error> OpenProfile(const ProfileId& id) = 0;
  // Asks the user first (natively), then deletes; `done` is called once.
  virtual void DeleteProfile(const ProfileId& id, DeleteDone done) = 0;
};

// Answers the profile manager page's bridge messages (web/profiles/api.ts):
// JSON objects with a "type". Everything in them comes from a web page, so
// it is checked: only the brouk://profiles page may ask, sizes and types are
// limited, ids must be valid profile ids. UI thread.
class ProfileManagerHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit ProfileManagerHandler(ProfileManagerDelegate* delegate)
      : delegate_(delegate) {}
  ProfileManagerHandler(const ProfileManagerHandler&) = delete;
  ProfileManagerHandler& operator=(const ProfileManagerHandler&) = delete;

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;

 private:
  ProfileManagerDelegate* delegate_;  // Not owned; outlives the page window.
};

}  // namespace brouk

#endif  // BROUK_CEF_PROFILE_MANAGER_HANDLER_H_
