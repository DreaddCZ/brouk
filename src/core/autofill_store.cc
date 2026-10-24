#include "core/autofill_store.h"

#include <algorithm>
#include <utility>

#include "core/autofill_rules.h"
#include "core/json.h"

namespace brouk {

namespace {

constexpr std::int64_t kSchemaVersion = 1;
constexpr std::string_view kKeyCheck = "brouk-autofill";
constexpr std::string_view kFormat = "brouk-autofill";
constexpr std::size_t kMaxValueBytes = 4096;

JsonValue EntryJson(const AutofillEntry& entry) {
  JsonValue json = JsonValue::Object();
  json.Add("label", JsonValue::String(entry.label));
  JsonValue values = JsonValue::Object();
  for (std::size_t i = 0; i < kAutofillStoredFields; ++i) {
    if (!entry.values[i].empty()) {
      values.Add(std::string(AutofillFieldName(static_cast<AutofillField>(i))),
                 JsonValue::String(entry.values[i]));
    }
  }
  json.Add("values", std::move(values));
  json.Add("origin_site", JsonValue::String(entry.origin_site));
  json.Add("created", JsonValue::Integer(entry.created));
  return json;
}

std::optional<AutofillEntry> EntryFromJson(const JsonValue& json) {
  if (json.kind != JsonValue::Kind::kObject) {
    return std::nullopt;
  }
  AutofillEntry entry;
  entry.label = json.StringOr("label", "").substr(0, kMaxValueBytes);
  entry.origin_site = json.StringOr("origin_site", "").substr(0, 255);
  entry.created = json.IntegerOr("created", 0);
  if (const JsonValue* values = json.Find("values");
      values && values->kind == JsonValue::Kind::kObject) {
    for (const JsonMember& member : values->members) {
      std::optional<AutofillField> field = AutofillFieldFromName(member.key);
      if (field && static_cast<std::size_t>(*field) < kAutofillStoredFields &&
          member.value.kind == JsonValue::Kind::kString) {
        entry.Set(*field, member.value.string.substr(0, kMaxValueBytes));
      }
    }
  }
  return entry;
}

JsonValue CustomJson(const CustomField& field) {
  JsonValue json = JsonValue::Object();
  json.Add("name", JsonValue::String(field.name));
  json.Add("value", JsonValue::String(field.value));
  JsonValue words = JsonValue::Array();
  for (const std::string& word : field.label_words) {
    words.items.push_back(JsonValue::String(word));
  }
  json.Add("label_words", std::move(words));
  return json;
}

std::optional<CustomField> CustomFromJson(const JsonValue& json) {
  if (json.kind != JsonValue::Kind::kObject) {
    return std::nullopt;
  }
  CustomField field;
  field.name = json.StringOr("name", "").substr(0, kMaxValueBytes);
  field.value = json.StringOr("value", "").substr(0, kMaxValueBytes);
  if (const JsonValue* words = json.Find("label_words");
      words && words->kind == JsonValue::Kind::kArray) {
    for (const JsonValue& word : words->items) {
      if (word.kind == JsonValue::Kind::kString && !word.string.empty() &&
          field.label_words.size() < 50) {
        field.label_words.push_back(word.string.substr(0, 200));
      }
    }
  }
  if (field.name.empty() && field.value.empty()) {
    return std::nullopt;
  }
  return field;
}

}  // namespace

AutofillStore::AutofillStore(Key /*key*/,
                             std::unique_ptr<SqliteDatabase> db,
                             const SecretKey& key)
    : db_(std::move(db)), key_(key) {}

Result<std::unique_ptr<AutofillStore>> AutofillStore::Open(
    const std::filesystem::path& path,
    const SecretKey& key) {
  Result<std::unique_ptr<SqliteDatabase>> db = SqliteDatabase::Open(path);
  if (!db) {
    return db.error();
  }
  auto store =
      std::make_unique<AutofillStore>(Key{}, std::move(db).value(), key);
  if (std::optional<Error> error = store->CreateSchema()) {
    return *error;
  }
  return store;
}

std::optional<Error> AutofillStore::CreateSchema() {
  if (std::optional<Error> error =
          db_->Execute("PRAGMA journal_mode=WAL; PRAGMA secure_delete=ON;")) {
    return error;
  }
  Error error;
  std::optional<SqliteStatement> version =
      db_->Prepare("PRAGMA user_version", error);
  if (!version || !version->Step()) {
    return error;
  }
  std::int64_t current = version->ColumnInt(0);
  if (current > kSchemaVersion) {
    return Error{"The autofill data was saved by a newer version of the app."};
  }
  if (current < kSchemaVersion) {
    if (std::optional<Error> created =
            db_->Execute("BEGIN;"
                         "CREATE TABLE IF NOT EXISTS entries("
                         "  id INTEGER PRIMARY KEY,"
                         "  data TEXT NOT NULL,"
                         "  created INTEGER NOT NULL DEFAULT 0,"
                         "  last_used INTEGER NOT NULL DEFAULT 0,"
                         "  times_used INTEGER NOT NULL DEFAULT 0);"
                         "CREATE TABLE IF NOT EXISTS custom_fields("
                         "  id INTEGER PRIMARY KEY,"
                         "  data TEXT NOT NULL,"
                         "  last_used INTEGER NOT NULL DEFAULT 0);"
                         "CREATE TABLE IF NOT EXISTS meta(name TEXT PRIMARY "
                         "KEY, value TEXT);"
                         "PRAGMA user_version=1;"
                         "COMMIT;")) {
      return created;
    }
  }
  std::optional<SqliteStatement> check =
      db_->Prepare("SELECT value FROM meta WHERE name = 'key_check'", error);
  if (!check) {
    return error;
  }
  if (check->Step()) {
    std::optional<std::string> plain =
        Unseal(check->ColumnText(0), "key_check");
    if (!plain || *plain != kKeyCheck) {
      return Error{
          "The autofill data was encrypted with a key that is no longer in "
          "the keychain."};
    }
    return std::nullopt;
  }
  std::optional<SqliteStatement> insert = db_->Prepare(
      "INSERT INTO meta(name, value) VALUES('key_check', ?)", error);
  if (!insert) {
    return error;
  }
  insert->Bind(1, Seal(std::string(kKeyCheck), "key_check"));
  if (!insert->Run()) {
    return Error{"Could not set up the autofill storage."};
  }
  return std::nullopt;
}

std::string AutofillStore::Seal(const std::string& json,
                                std::string_view table) const {
  return Base64Encode(Encrypt(key_, json, table));
}

std::optional<std::string> AutofillStore::Unseal(const std::string& data,
                                                 std::string_view table) const {
  std::optional<std::string> sealed = Base64Decode(data);
  return sealed ? Decrypt(key_, *sealed, table) : std::nullopt;
}

std::vector<AutofillEntry> AutofillStore::Entries() {
  std::vector<AutofillEntry> entries;
  Error error;
  std::optional<SqliteStatement> statement = db_->Prepare(
      "SELECT id, data, created, last_used, times_used FROM entries "
      "ORDER BY id",
      error);
  while (statement && statement->Step()) {
    std::optional<std::string> json =
        Unseal(statement->ColumnText(1), "entries");
    Result<JsonValue> parsed =
        json ? ParseJsonDocument(*json) : Result<JsonValue>(Error{""});
    std::optional<AutofillEntry> entry =
        parsed ? EntryFromJson(parsed.value()) : std::nullopt;
    if (!entry) {
      continue;  // Damaged: left out rather than shown as garbage.
    }
    entry->id = statement->ColumnInt(0);
    entry->created = statement->ColumnInt(2);
    entry->last_used = statement->ColumnInt(3);
    entry->times_used = statement->ColumnInt(4);
    entries.push_back(std::move(*entry));
  }
  return entries;
}

std::optional<AutofillEntry> AutofillStore::Entry(std::int64_t id) {
  for (AutofillEntry& entry : Entries()) {
    if (entry.id == id) {
      return std::move(entry);
    }
  }
  return std::nullopt;
}

Result<std::int64_t> AutofillStore::AddEntry(const AutofillEntry& entry,
                                             std::int64_t now) {
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("INSERT INTO entries(data, created) VALUES(?, ?)", error);
  if (!statement) {
    return error;
  }
  statement->Bind(1, Seal(WriteJsonDocument(EntryJson(entry)), "entries"));
  statement->Bind(2, entry.created > 0 ? entry.created : now);
  if (!statement->Run()) {
    return Error{"Could not save the entry."};
  }
  return db_->last_insert_id();
}

std::optional<Error> AutofillStore::UpdateEntry(const AutofillEntry& entry) {
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("UPDATE entries SET data = ? WHERE id = ?", error);
  if (!statement) {
    return error;
  }
  statement->Bind(1, Seal(WriteJsonDocument(EntryJson(entry)), "entries"));
  statement->Bind(2, entry.id);
  if (!statement->Run() || db_->changes() == 0) {
    return Error{"The entry no longer exists."};
  }
  return std::nullopt;
}

std::vector<AutofillEntry> AutofillStore::RemoveEntries(
    std::span<const std::int64_t> ids) {
  std::vector<AutofillEntry> removed;
  std::vector<AutofillEntry> all = Entries();
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("DELETE FROM entries WHERE id = ?", error);
  for (std::int64_t id : ids) {
    auto it = std::ranges::find_if(
        all, [id](const AutofillEntry& e) { return e.id == id; });
    if (it == all.end() || !statement) {
      continue;
    }
    statement->Reset();
    statement->Bind(1, id);
    if (statement->Run()) {
      removed.push_back(*it);
    }
  }
  return removed;
}

void AutofillStore::RestoreEntries(const std::vector<AutofillEntry>& entries) {
  Error error;
  std::optional<SqliteStatement> statement = db_->Prepare(
      "INSERT OR REPLACE INTO entries(id, data, created, last_used, "
      "times_used) VALUES(?, ?, ?, ?, ?)",
      error);
  for (const AutofillEntry& entry : entries) {
    if (!statement) {
      return;
    }
    statement->Reset();
    statement->Bind(1, entry.id);
    statement->Bind(2, Seal(WriteJsonDocument(EntryJson(entry)), "entries"));
    statement->Bind(3, entry.created);
    statement->Bind(4, entry.last_used);
    statement->Bind(5, entry.times_used);
    statement->Run();
  }
}

void AutofillStore::MarkEntryUsed(std::int64_t id, std::int64_t now) {
  Error error;
  std::optional<SqliteStatement> statement = db_->Prepare(
      "UPDATE entries SET last_used = ?, times_used = times_used + 1 "
      "WHERE id = ?",
      error);
  if (statement) {
    statement->Bind(1, now);
    statement->Bind(2, id);
    statement->Run();
  }
}

std::vector<CustomField> AutofillStore::CustomFields() {
  std::vector<CustomField> fields;
  Error error;
  std::optional<SqliteStatement> statement = db_->Prepare(
      "SELECT id, data, last_used FROM custom_fields ORDER BY id", error);
  while (statement && statement->Step()) {
    std::optional<std::string> json =
        Unseal(statement->ColumnText(1), "custom_fields");
    Result<JsonValue> parsed =
        json ? ParseJsonDocument(*json) : Result<JsonValue>(Error{""});
    std::optional<CustomField> field =
        parsed ? CustomFromJson(parsed.value()) : std::nullopt;
    if (!field) {
      continue;
    }
    field->id = statement->ColumnInt(0);
    field->last_used = statement->ColumnInt(2);
    fields.push_back(std::move(*field));
  }
  return fields;
}

Result<std::int64_t> AutofillStore::AddCustomField(const CustomField& field) {
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("INSERT INTO custom_fields(data) VALUES(?)", error);
  if (!statement) {
    return error;
  }
  statement->Bind(1,
                  Seal(WriteJsonDocument(CustomJson(field)), "custom_fields"));
  if (!statement->Run()) {
    return Error{"Could not save the custom field."};
  }
  return db_->last_insert_id();
}

std::optional<Error> AutofillStore::UpdateCustomField(
    const CustomField& field) {
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("UPDATE custom_fields SET data = ? WHERE id = ?", error);
  if (!statement) {
    return error;
  }
  statement->Bind(1,
                  Seal(WriteJsonDocument(CustomJson(field)), "custom_fields"));
  statement->Bind(2, field.id);
  if (!statement->Run() || db_->changes() == 0) {
    return Error{"The custom field no longer exists."};
  }
  return std::nullopt;
}

std::vector<CustomField> AutofillStore::RemoveCustomFields(
    std::span<const std::int64_t> ids) {
  std::vector<CustomField> removed;
  std::vector<CustomField> all = CustomFields();
  Error error;
  std::optional<SqliteStatement> statement =
      db_->Prepare("DELETE FROM custom_fields WHERE id = ?", error);
  for (std::int64_t id : ids) {
    auto it = std::ranges::find_if(
        all, [id](const CustomField& f) { return f.id == id; });
    if (it == all.end() || !statement) {
      continue;
    }
    statement->Reset();
    statement->Bind(1, id);
    if (statement->Run()) {
      removed.push_back(*it);
    }
  }
  return removed;
}

void AutofillStore::RestoreCustomFields(
    const std::vector<CustomField>& fields) {
  Error error;
  std::optional<SqliteStatement> statement = db_->Prepare(
      "INSERT OR REPLACE INTO custom_fields(id, data, last_used) "
      "VALUES(?, ?, ?)",
      error);
  for (const CustomField& field : fields) {
    if (!statement) {
      return;
    }
    statement->Reset();
    statement->Bind(1, field.id);
    statement->Bind(
        2, Seal(WriteJsonDocument(CustomJson(field)), "custom_fields"));
    statement->Bind(3, field.last_used);
    statement->Run();
  }
}

void AutofillStore::MarkCustomFieldUsed(std::int64_t id, std::int64_t now) {
  Error error;
  std::optional<SqliteStatement> statement = db_->Prepare(
      "UPDATE custom_fields SET last_used = ? WHERE id = ?", error);
  if (statement) {
    statement->Bind(1, now);
    statement->Bind(2, id);
    statement->Run();
  }
}

std::string WriteAutofillJson(const std::vector<AutofillEntry>& entries,
                              const std::vector<CustomField>& fields) {
  JsonValue root = JsonValue::Object();
  root.Add("format", JsonValue::String(std::string(kFormat)));
  root.Add("version", JsonValue::Integer(1));
  JsonValue list = JsonValue::Array();
  for (const AutofillEntry& entry : entries) {
    list.items.push_back(EntryJson(entry));
  }
  root.Add("entries", std::move(list));
  JsonValue custom = JsonValue::Array();
  for (const CustomField& field : fields) {
    custom.items.push_back(CustomJson(field));
  }
  root.Add("custom_fields", std::move(custom));
  return WriteJsonDocument(root);
}

Result<AutofillJson> ParseAutofillJson(std::string_view text) {
  Result<JsonValue> parsed = ParseJsonDocument(text);
  if (!parsed || parsed.value().kind != JsonValue::Kind::kObject ||
      parsed.value().StringOr("format", "") != kFormat) {
    return Error{"This file is not a Brouk autofill export."};
  }
  if (parsed.value().IntegerOr("version", 0) > 1) {
    return Error{
        "This file was exported by a newer version of Brouk. Update Brouk to "
        "import it."};
  }
  AutofillJson result;
  if (const JsonValue* entries = parsed.value().Find("entries");
      entries && entries->kind == JsonValue::Kind::kArray) {
    for (const JsonValue& item : entries->items) {
      std::optional<AutofillEntry> entry = EntryFromJson(item);
      if (entry && !entry->Empty()) {
        result.entries.push_back(std::move(*entry));
      }
    }
  }
  if (const JsonValue* fields = parsed.value().Find("custom_fields");
      fields && fields->kind == JsonValue::Kind::kArray) {
    for (const JsonValue& item : fields->items) {
      if (std::optional<CustomField> field = CustomFromJson(item)) {
        result.custom_fields.push_back(std::move(*field));
      }
    }
  }
  return result;
}

AutofillImportPlan PlanAutofillImport(const AutofillJson& incoming,
                                      const std::vector<AutofillEntry>& entries,
                                      const std::vector<CustomField>& fields,
                                      std::string_view country) {
  AutofillImportPlan plan;
  // Merges change saved entries; later imported entries compare with them.
  std::vector<AutofillEntry> current = entries;
  for (const AutofillEntry& entry : incoming.entries) {
    AutofillValues values;
    for (std::size_t i = 0; i < kAutofillStoredFields; ++i) {
      if (!entry.values[i].empty()) {
        values[static_cast<AutofillField>(i)] = entry.values[i];
      }
    }
    AutofillComparison result = CompareWithEntries(values, current, country);
    if (result.kind == AutofillComparison::Kind::kSame) {
      ++plan.skipped;
    } else if (result.kind == AutofillComparison::Kind::kAddsInformation &&
               result.entry && current[*result.entry].id != 0) {
      AutofillEntry& target = current[*result.entry];
      for (AutofillField field : result.new_fields) {
        target.Set(field, entry.Get(field));
      }
      auto known = std::ranges::find_if(
          plan.merged, [&](const auto& e) { return e.id == target.id; });
      if (known == plan.merged.end()) {
        plan.merged.push_back(target);
      } else {
        *known = target;
      }
    } else {
      plan.to_add.push_back(entry);
      current.push_back(entry);
      current.back().id = 0;
    }
  }
  std::vector<CustomField> known_fields = fields;
  for (const CustomField& field : incoming.custom_fields) {
    bool exists = std::ranges::any_of(known_fields, [&](const CustomField& f) {
      return f.name == field.name && f.value == field.value;
    });
    if (exists) {
      ++plan.custom_skipped;
    } else {
      plan.custom_to_add.push_back(field);
      known_fields.push_back(field);
    }
  }
  return plan;
}

}  // namespace brouk
