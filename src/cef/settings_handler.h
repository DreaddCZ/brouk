#ifndef BROUK_CEF_SETTINGS_HANDLER_H_
#define BROUK_CEF_SETTINGS_HANDLER_H_

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/profile_registry.h"
#include "core/result.h"
#include "core/settings.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// What the settings page (brouk://settings, 004) can ask for. Implemented by
// the platform, which owns the settings and applies changes. UI thread.
class SettingsPageDelegate {
 public:
  // finished is false when the user cancelled a panel; `problems` lists what
  // an import left out.
  using FileDone = std::function<void(bool finished,
                                      std::vector<std::string> problems,
                                      std::optional<Error> error)>;

  virtual ~SettingsPageDelegate() = default;
  // Profiles, global settings and the problems found in the files.
  virtual const ProfileRegistry& Registry() = 0;
  virtual std::optional<Settings> ProfileSettings(std::string_view id) = 0;
  virtual std::optional<Error> SetGlobalSetting(std::string_view key,
                                                SettingValue value) = 0;
  virtual std::optional<Error> SetProfileSetting(std::string_view id,
                                                 std::string_view key,
                                                 SettingValue value) = 0;
  // Shows the settings files in Finder (for advanced settings).
  virtual void RevealSettingsFolder() = 0;
  virtual void ExportSettings(FileDone done) = 0;
  virtual void ImportSettings(FileDone done) = 0;
};

// Answers the settings page's bridge messages (web/settings/api.ts). The
// page shows every non-advanced setting from the schema, so the page and
// the files never disagree (004). Everything in a message is checked: only
// brouk://settings may ask, sizes and types are limited, and values are
// validated against the schema. UI thread.
class SettingsHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit SettingsHandler(SettingsPageDelegate* delegate)
      : delegate_(delegate) {}
  SettingsHandler(const SettingsHandler&) = delete;
  SettingsHandler& operator=(const SettingsHandler&) = delete;

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;

 private:
  SettingsPageDelegate* delegate_;  // Not owned; outlives the page window.
};

}  // namespace brouk

#endif  // BROUK_CEF_SETTINGS_HANDLER_H_
