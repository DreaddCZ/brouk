#ifndef BROUK_CORE_AUTOFILL_STORE_H_
#define BROUK_CORE_AUTOFILL_STORE_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/autofill_entry.h"
#include "core/crypto.h"
#include "core/result.h"
#include "core/sqlite_db.h"

namespace brouk {

// The app's autofill data (009) in autofill.db: entries and custom fields,
// shared by all profiles. Every value (labels included) is encrypted with
// the app's autofill key from the keychain; only ids and usage counts are
// stored plain. Not thread-safe: used from one thread only.
class AutofillStore {
  struct Key {};

 public:
  AutofillStore(Key /*key*/,
                std::unique_ptr<SqliteDatabase> db,
                const SecretKey& key);
  // Fails if the file cannot be opened, comes from a newer version, or was
  // encrypted with another key.
  static Result<std::unique_ptr<AutofillStore>> Open(
      const std::filesystem::path& path,
      const SecretKey& key);

  std::vector<AutofillEntry> Entries();
  std::optional<AutofillEntry> Entry(std::int64_t id);
  // Its id is ignored; `now` sets the created time unless one is given.
  Result<std::int64_t> AddEntry(const AutofillEntry& entry, std::int64_t now);
  std::optional<Error> UpdateEntry(const AutofillEntry& entry);
  // Removes entries and returns them, for undo.
  std::vector<AutofillEntry> RemoveEntries(std::span<const std::int64_t> ids);
  void RestoreEntries(const std::vector<AutofillEntry>& entries);
  void MarkEntryUsed(std::int64_t id, std::int64_t now);

  std::vector<CustomField> CustomFields();
  Result<std::int64_t> AddCustomField(const CustomField& field);
  std::optional<Error> UpdateCustomField(const CustomField& field);
  std::vector<CustomField> RemoveCustomFields(
      std::span<const std::int64_t> ids);
  void RestoreCustomFields(const std::vector<CustomField>& fields);
  void MarkCustomFieldUsed(std::int64_t id, std::int64_t now);

 private:
  std::optional<Error> CreateSchema();
  std::string Seal(const std::string& json, std::string_view table) const;
  std::optional<std::string> Unseal(const std::string& data,
                                    std::string_view table) const;

  std::unique_ptr<SqliteDatabase> db_;
  SecretKey key_;
};

// The export file (JSON) for backup and moving to another Mac:
//   {"format": "brouk-autofill", "version": 1,
//    "entries": [{"label": …, "values": {"first_name": …}, "origin_site": …,
//                 "created": …}],
//    "custom_fields": [{"name": …, "value": …, "label_words": [...]}]}
std::string WriteAutofillJson(const std::vector<AutofillEntry>& entries,
                              const std::vector<CustomField>& fields);
struct AutofillJson {
  std::vector<AutofillEntry> entries;
  std::vector<CustomField> custom_fields;
};
Result<AutofillJson> ParseAutofillJson(std::string_view text);

// What importing a file does, by the rules of saving (009): entries equal to
// a saved one are skipped, entries adding fields to one are merged into it,
// others are added. Custom fields with the same name and value are skipped.
struct AutofillImportPlan {
  std::vector<AutofillEntry> to_add;
  std::vector<AutofillEntry> merged;  // Saved entries with added fields.
  int skipped = 0;
  std::vector<CustomField> custom_to_add;
  int custom_skipped = 0;
};
AutofillImportPlan PlanAutofillImport(const AutofillJson& incoming,
                                      const std::vector<AutofillEntry>& entries,
                                      const std::vector<CustomField>& fields,
                                      std::string_view country);

}  // namespace brouk

#endif  // BROUK_CORE_AUTOFILL_STORE_H_
