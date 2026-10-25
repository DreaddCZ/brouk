#ifndef BROUK_CEF_PASSWORD_TAB_H_
#define BROUK_CEF_PASSWORD_TAB_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cef/password_manager.h"
#include "core/password_forms.h"
#include "core/profile_id.h"
#include "include/cef_browser.h"

namespace brouk {

// One entry of the password drop-down (008).
struct PasswordMenuItem {
  enum class Kind {
    kLogin,
    kGeneratedPassword,
    kCode,
    kReadQrCode,
    kManage,
    kWarning
  };
  Kind kind = Kind::kLogin;
  std::string title;     // User name, "Use a strong password", …
  std::string detail;    // The host for other hosts; the generated password;
                         // the code.
  int seconds_left = 0;  // kCode: until the code changes.
};

// The drop-down below a login field, or below the key icon.
struct PasswordMenu {
  // The field's box in the tab view, in points from its top left. Unused
  // when `at_key_icon`.
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
  bool at_key_icon = false;
  std::vector<PasswordMenuItem> items;
};
// The chosen item's index, or -1 when the menu closed without a choice.
using PasswordMenuDone = std::function<void(int index)>;

// The save prompt, anchored to the key icon. The user can edit both values.
struct PasswordPrompt {
  bool update = false;  // "Update password?" instead of "Save password?".
  std::string host;
  std::string username;
  std::string password;
};
struct PasswordPromptAnswer {
  enum class Choice { kSave, kNever, kNotNow };
  Choice choice = Choice::kNotNow;
  std::string username;
  std::string password;
};
using PasswordPromptDone = std::function<void(PasswordPromptAnswer)>;

// The key icon in the address bar.
enum class PasswordIcon { kHidden, kLogins, kSavePending };

// What the password logic of a tab needs from the platform UI. UI thread.
class PasswordTabDelegate {
 public:
  virtual ~PasswordTabDelegate() = default;
  virtual void ShowPasswordMenu(const PasswordMenu& menu,
                                PasswordMenuDone done) = 0;
  // The field lost the focus: close the menu unless the user is using it.
  virtual void HidePasswordMenu() = 0;
  virtual void ShowPasswordPrompt(const PasswordPrompt& prompt,
                                  PasswordPromptDone done) = 0;
  virtual void OnPasswordIconChanged(PasswordIcon icon) = 0;
  virtual void OpenPasswordsPage() = 0;
  virtual void CopyTextToClipboard(const std::string& text) = 0;
  // A key went down in the page (before the page sees it): Up, Down,
  // Return or Escape move through and choose from an open menu. Returns
  // whether the menu used the key.
  virtual bool HandlePasswordMenuKey(int windows_key_code) = 0;
  // The texts of the QR codes in a PNG image of the page (2FA setup, 008).
  using QrDone = std::function<void(std::vector<std::string> texts)>;
  virtual void DecodeQrCodes(std::vector<std::uint8_t> png, QrDone done) = 0;
  // A short message about a password action, e.g. "2FA code added".
  virtual void ShowPasswordMessage(const std::string& title,
                                   const std::string& text) = 0;
};

// The password manager's work in one tab (008): looks up the logins of the
// frames' login forms, fills them (automatically or from the drop-down),
// and offers to save a submitted login once the page moved on. Owned by the
// tab's TabClient; UI thread only.
class PasswordTab : public std::enable_shared_from_this<PasswordTab> {
 public:
  PasswordTab(PasswordManager* manager,
              ProfileId profile,
              PasswordTabDelegate* delegate);
  PasswordTab(const PasswordTab&) = delete;
  PasswordTab& operator=(const PasswordTab&) = delete;

  // A kFormsMessage from a frame's page script (untrusted).
  void HandleMessage(const CefRefPtr<CefBrowser>& browser,
                     const CefRefPtr<CefFrame>& frame,
                     const std::string& kind,
                     const std::string& json);
  // A new document starts loading in the main frame: a submitted login now
  // counts as successful; the old forms are gone.
  void OnMainFrameLoadStart();
  // The key icon was clicked: the save prompt if one is waiting, else the
  // site's logins.
  void OnKeyIconClicked();
  // The tab is closing: no more calls to the delegate.
  void Detach();

 private:
  struct Frame {
    std::string origin;
    bool main = false;
    std::vector<LoginForm> forms;
    std::optional<PasswordLookup> lookup;
    std::vector<int> filled_forms;  // Filled automatically already.
  };
  struct Submitted {
    std::string frame_id;
    std::string origin;
    SubmittedLogin login;
    std::int64_t time = 0;
  };

  void OnForms(const CefRefPtr<CefFrame>& frame, const std::string& json);
  void OnFocus(const CefRefPtr<CefFrame>& frame, const std::string& json);
  void OnSubmit(const CefRefPtr<CefFrame>& frame, const std::string& json);
  void LookUp(const std::string& frame_id);
  void TryAutofill(const std::string& frame_id);
  void Fill(const std::string& frame_id,
            const LoginForm& form,
            const PasswordEntry& entry);
  void SendFill(const std::string& frame_id, const std::string& message);
  void EvaluateSubmitted();
  void ShowPrompt();
  void UpdateIcon();
  // Reads a 2FA setup QR code from the page's rendering and adds its secret
  // to one of `entries` (asking which when there are several).
  void ReadQrCode(std::vector<PasswordEntry> entries);
  void OnQrCodes(const std::vector<PasswordEntry>& entries,
                 const std::vector<std::string>& texts);
  void AddTotp(PasswordEntry entry, const std::string& totp);
  // The breach check when a password is saved (008): a warning if it is
  // known from breaches.
  void WarnIfBreached(const std::string& host, const std::string& password);
  // The user name of the previous step of a multi-step login on the same
  // site, if recent.
  std::string RememberedUsername(const std::string& origin) const;
  std::vector<PasswordEntry> Offered(const Frame& frame) const;

  PasswordManager* manager_;  // Outlives every tab.
  ProfileId profile_;
  PasswordTabDelegate* delegate_;  // Null after Detach().
  CefRefPtr<CefBrowser> browser_;
  std::map<std::string, Frame> frames_;  // By frame identifier.
  std::optional<Submitted> submitted_;
  struct Remembered {
    std::string site;
    std::string username;
    std::int64_t time = 0;
  };
  std::optional<Remembered> remembered_;
  // A generated password saved at once (Chrome does so), still without the
  // user name that the submit tells.
  struct Generated {
    std::int64_t id = 0;
    std::string origin;
    std::string password;
  };
  std::optional<Generated> generated_;
  // The save prompt waiting for an answer, shown again from the key icon.
  struct Prompt {
    PasswordPrompt prompt;
    std::string origin;
    std::optional<PasswordEntry> existing;  // For an update.
    std::int64_t time = 0;
  };
  std::optional<Prompt> prompt_;
};

}  // namespace brouk

#endif  // BROUK_CEF_PASSWORD_TAB_H_
