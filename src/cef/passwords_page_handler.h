#ifndef BROUK_CEF_PASSWORDS_PAGE_HANDLER_H_
#define BROUK_CEF_PASSWORDS_PAGE_HANDLER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cef/password_manager.h"
#include "core/profile_id.h"
#include "core/result.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// A profile as the passwords page shows it.
struct PasswordsPageProfile {
  ProfileId id;
  std::string name;
  std::string color;
  bool bitwarden = false;  // Uses Bitwarden instead of built-in passwords.
};

// What the passwords page (brouk://passwords, 008) needs from the browser;
// one per page tab. UI thread.
class PasswordsPageDelegate {
 public:
  virtual ~PasswordsPageDelegate() = default;
  virtual std::vector<PasswordsPageProfile> Profiles() = 0;
  // The page tab's profile: the profile filter starts there.
  virtual ProfileId CurrentProfile() = 0;
  // Null before CEF is ready.
  virtual PasswordManager* Manager() = 0;
  virtual bool BreachCheckEnabled() = 0;
  // Touch ID or the macOS password; `done` gets whether it succeeded.
  virtual void Authenticate(const std::string& reason,
                            std::function<void(bool)> done) = 0;
  virtual void CopyToClipboard(const std::string& text) = 0;
  // Export: asks where to save `csv`. Import: asks for a CSV file.
  using SaveDone = std::function<void(bool saved, std::optional<Error>)>;
  virtual void SaveCsv(std::string csv, SaveDone done) = 0;
  using OpenDone = std::function<void(std::optional<std::string> text,
                                      std::optional<Error>)>;
  virtual void OpenCsv(OpenDone done) = 0;
};

// Answers the passwords page's bridge messages (web/passwords/api.ts).
// Only brouk://passwords may ask. Revealing, copying, editing and exporting
// passwords, and the checkup, need the user's OS authentication; after it
// the page stays unlocked for 5 minutes (008). UI thread.
class PasswordsPageHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit PasswordsPageHandler(
      std::unique_ptr<PasswordsPageDelegate> delegate);
  PasswordsPageHandler(const PasswordsPageHandler&) = delete;
  PasswordsPageHandler& operator=(const PasswordsPageHandler&) = delete;

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;

 private:
  struct State;
  // Shared with callbacks that outlive one message.
  std::shared_ptr<State> state_;
};

}  // namespace brouk

#endif  // BROUK_CEF_PASSWORDS_PAGE_HANDLER_H_
