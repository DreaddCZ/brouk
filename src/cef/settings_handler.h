#ifndef BROUK_CEF_SETTINGS_HANDLER_H_
#define BROUK_CEF_SETTINGS_HANDLER_H_

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cef/adblock_service.h"
#include "cef/autofill_manager.h"
#include "core/link_routing_rules.h"
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
  // Clear browsing data (013), on the history page.
  virtual void OpenClearBrowsingData() = 0;
  // Ad blocking (007): null before the browser is ready; the system
  // language (for the regional list).
  virtual AdblockService* Adblock() = 0;
  virtual std::string Language() = 0;
  // Form autofill (009): null before the browser is ready; asking where to
  // save the JSON export, and for a file to import.
  virtual AutofillManager* Autofill() = 0;
  using TextSaveDone =
      std::function<void(bool saved, const std::optional<Error>& error)>;
  virtual void SaveAutofillFile(std::string json, TextSaveDone done) = 0;
  using TextOpenDone =
      std::function<void(const std::optional<std::string>& text,
                         const std::optional<Error>& error)>;
  virtual void OpenAutofillFile(TextOpenDone done) = 0;
  virtual void ExportSettings(FileDone done) = 0;
  virtual void ImportSettings(FileDone done) = 0;
  // Link routing (010): null before the browser is ready; saving replaces
  // the rules file.
  virtual const LinkRoutingConfig* LinkRouting() = 0;
  virtual std::vector<std::string> LinkRoutingProblems() = 0;
  virtual std::optional<Error> SetLinkRouting(LinkRoutingConfig config) = 0;
  // Brouk opens http and https links from other apps. Making it the
  // default asks the user (macOS); `done` gets an error if it did not
  // happen.
  virtual bool IsDefaultBrowser() = 0;
  virtual void MakeDefaultBrowser(
      std::function<void(std::optional<Error> error)> done) = 0;
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
