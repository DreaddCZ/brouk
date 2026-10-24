#ifndef BROUK_CEF_AUTOFILL_MANAGER_H_
#define BROUK_CEF_AUTOFILL_MANAGER_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/autofill_entry.h"
#include "core/autofill_store.h"
#include "core/result.h"
#include "core/secret_store.h"
#include "include/cef_thread.h"

namespace brouk {

// Settings autofill follows (009), from the global settings.
struct AutofillOptions {
  bool enabled = true;
  bool entries = true;
  bool custom_fields = true;
  bool offer_to_save = true;
  std::string country;  // For phone numbers, e.g. "CZ".
};

// The app's autofill data (009), used from the UI thread. The entries and
// custom fields are kept in memory once loaded (they are few and the
// drop-down and context menu need them at once); every change is written
// to autofill.db on a dedicated thread. Values are never logged.
class AutofillManager {
 public:
  // UI thread. Loads the data on the autofill thread.
  AutofillManager(std::filesystem::path database,
                  std::shared_ptr<SecretStore> keychain);
  ~AutofillManager();
  AutofillManager(const AutofillManager&) = delete;
  AutofillManager& operator=(const AutofillManager&) = delete;

  const AutofillOptions& options() const { return options_; }
  void SetOptions(const AutofillOptions& options) { options_ = options; }

  // False until loaded; `error` when the data could not be opened.
  bool loaded() const { return loaded_; }
  const std::optional<std::string>& error() const { return error_; }
  const std::vector<AutofillEntry>& entries() const { return entries_; }
  const std::vector<CustomField>& custom_fields() const { return custom_; }

  // Changes; `done` (optional) runs once written, with the new id or the
  // error. The in-memory lists change at once for updates and removals,
  // after writing for additions (they need their id).
  using IdDone = std::function<void(Result<std::int64_t>)>;
  void AddEntry(AutofillEntry entry, IdDone done);
  void UpdateEntry(const AutofillEntry& entry);
  // Removes entries; the last removal (entries and custom fields) can be
  // undone.
  void RemoveEntries(const std::vector<std::int64_t>& ids);
  void MarkEntryUsed(std::int64_t id);
  void AddCustomField(CustomField field, IdDone done);
  void UpdateCustomField(const CustomField& field);
  void RemoveCustomFields(const std::vector<std::int64_t>& ids);
  void MarkCustomFieldUsed(std::int64_t id);
  bool CanUndo() const {
    return !removed_entries_.empty() || !removed_custom_.empty();
  }
  void Undo();

  // Called after every change (the settings page refreshes).
  void SetChangedCallback(std::function<void()> changed) {
    changed_ = std::move(changed);
  }

 private:
  struct State;
  void Post(std::function<void(State&)> work);
  void Changed();

  CefRefPtr<CefThread> thread_;
  std::shared_ptr<State> state_;
  std::shared_ptr<bool> alive_;
  AutofillOptions options_;
  bool loaded_ = false;
  std::optional<std::string> error_;
  std::vector<AutofillEntry> entries_;
  std::vector<CustomField> custom_;
  std::vector<AutofillEntry> removed_entries_;
  std::vector<CustomField> removed_custom_;
  std::function<void()> changed_;
};

}  // namespace brouk

#endif  // BROUK_CEF_AUTOFILL_MANAGER_H_
