#ifndef BROUK_CEF_AUTOFILL_TAB_H_
#define BROUK_CEF_AUTOFILL_TAB_H_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cef/autofill_manager.h"
#include "cef/password_tab.h"
#include "core/autofill_forms.h"
#include "core/autofill_rules.h"
#include "include/cef_browser.h"

namespace brouk {

// The prompt after a form was submitted (009): "Add to <label>?",
// "Update <label>?" or "Save as new entry?".
struct AutofillPrompt {
  enum class Kind { kAddTo, kUpdate, kSaveNew };
  Kind kind = Kind::kSaveNew;
  std::string entry_label;      // kAddTo, kUpdate.
  std::string suggested_label;  // kSaveNew (the user can change it).
  struct Line {
    std::string field;      // "Phone".
    std::string old_value;  // kUpdate: the saved value.
    std::string new_value;
    std::string problem;  // A failed validation, shown, never corrected.
  };
  std::vector<Line> lines;
};
struct AutofillPromptAnswer {
  // kAccept: Add / Update / Save. kSaveAsNew: on the update prompt.
  enum class Choice { kAccept, kSaveAsNew, kNotNow, kEdit };
  Choice choice = Choice::kNotNow;
  std::string label;  // For a new entry.
};
using AutofillPromptDone = std::function<void(AutofillPromptAnswer)>;

// The autofill UI the platform provides for a tab (the drop-down comes from
// PasswordTabDelegate). UI thread.
class AutofillTabDelegate {
 public:
  virtual ~AutofillTabDelegate() = default;
  virtual void ShowAutofillPrompt(const AutofillPrompt& prompt,
                                  AutofillPromptDone done) = 0;
  // Settings → Autofill.
  virtual void OpenAutofillSettings() = 0;
};

// Autofill in one tab (009): the frames' recognised form fields, the
// drop-down on focus, filling a whole form from an entry (highlighted, with
// "Clear filled fields"), custom fields, and the offer to save after a
// submit. Owned by the tab's TabClient; UI thread only.
class AutofillTab : public std::enable_shared_from_this<AutofillTab> {
 public:
  AutofillTab(AutofillManager* manager,
              PasswordTabDelegate* menus,
              AutofillTabDelegate* delegate);
  AutofillTab(const AutofillTab&) = delete;
  AutofillTab& operator=(const AutofillTab&) = delete;

  // An "af…" kFormsMessage from a frame's page script (untrusted).
  void HandleMessage(const CefRefPtr<CefBrowser>& browser,
                     const CefRefPtr<CefFrame>& frame,
                     const std::string& kind,
                     const std::string& json);
  void OnMainFrameLoadStart();
  void Detach();

 private:
  struct Frame {
    std::string origin;
    std::vector<AutofillForm> forms;
    std::map<int, std::vector<int>> filled;  // By form: for "Clear".
  };

  void OnForms(const CefRefPtr<CefFrame>& frame, const std::string& json);
  void OnFocus(const CefRefPtr<CefFrame>& frame, const std::string& json);
  void OnSubmit(const CefRefPtr<CefFrame>& frame, const std::string& json);
  void FillForm(const std::string& frame_id, int form_id, std::int64_t entry);
  void ClearForm(const std::string& frame_id, int form_id);
  void Send(const std::string& frame_id, const std::string& message);
  void Ask(AutofillPrompt prompt,
           std::optional<std::int64_t> entry,
           AutofillValues stored,
           std::string site);

  AutofillManager* manager_;       // Outlives every tab.
  PasswordTabDelegate* menus_;     // Null after Detach().
  AutofillTabDelegate* delegate_;  // Null after Detach().
  CefRefPtr<CefBrowser> browser_;
  std::map<std::string, Frame> frames_;
  std::string last_submit_;  // Not offered twice for the same values.
};

}  // namespace brouk

#endif  // BROUK_CEF_AUTOFILL_TAB_H_
