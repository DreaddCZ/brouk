#include "cef/passwords_page_handler.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cef/breach_check.h"
#include "cef/json_util.h"
#include "core/address_input.h"
#include "core/crypto.h"
#include "core/internal_pages.h"
#include "core/password_checkup.h"
#include "core/password_csv.h"
#include "core/totp.h"
#include "core/web_origin.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxRequestBytes = std::size_t{256} * 1024;
constexpr std::size_t kMaxIds = 10000;
constexpr std::size_t kMaxValueBytes = 1024;
constexpr std::size_t kMaxNoteBytes = std::size_t{16} * 1024;
constexpr std::size_t kMaxOrigins = 50;
constexpr std::size_t kMaxImportBytes = std::size_t{16} * 1024 * 1024;
// After Touch ID or the password, the page stays unlocked this long (008).
constexpr std::int64_t kUnlockSeconds = std::int64_t{5} * 60;
constexpr int kBadRequest = 400;
constexpr int kLocked = 401;
constexpr int kFailed = 500;
constexpr double kMaxId = 9.0e15;

using Callback = CefMessageRouterBrowserSide::Callback;

std::int64_t Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::optional<std::string> StringField(
    const CefRefPtr<CefDictionaryValue>& message,
    const char* key,
    std::size_t max_bytes) {
  if (message->GetType(key) != VTYPE_STRING) {
    return std::nullopt;
  }
  std::string value = message->GetString(key).ToString();
  if (value.size() > max_bytes) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::int64_t> NumberOf(const CefRefPtr<CefValue>& value) {
  if (value->GetType() == VTYPE_INT) {
    return value->GetInt();
  }
  if (value->GetType() == VTYPE_DOUBLE) {
    double number = value->GetDouble();
    if (number >= 0 && number < kMaxId &&
        number == static_cast<double>(static_cast<std::int64_t>(number))) {
      return static_cast<std::int64_t>(number);
    }
  }
  return std::nullopt;
}

std::optional<std::int64_t> IdField(
    const CefRefPtr<CefDictionaryValue>& message,
    const char* key) {
  if (!message->HasKey(key)) {
    return std::nullopt;
  }
  return NumberOf(message->GetValue(key));
}

std::string ToJson(const CefRefPtr<CefValue>& value) {
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return ToJson(value);
}

std::string ToJson(const CefRefPtr<CefListValue>& list) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetList(list);
  return ToJson(value);
}

// An origin from what the user typed ("example.com/login" ->
// "https://example.com").
std::optional<std::string> OriginFromTyped(const std::string& typed) {
  if (std::optional<std::string> origin = WebOriginOf(typed)) {
    return origin;
  }
  AddressInputResult result = ClassifyAddressInput(typed);
  if (result.kind != AddressInputResult::Kind::kUrl) {
    return std::nullopt;
  }
  return WebOriginOf(result.url);
}

CefRefPtr<CefListValue> OriginList(const std::vector<std::string>& origins) {
  CefRefPtr<CefListValue> list = CefListValue::Create();
  for (const std::string& origin : origins) {
    list->SetString(list->GetSize(), origin);
  }
  return list;
}

// One entry without its secrets.
CefRefPtr<CefDictionaryValue> EntryRow(const ProfileId& profile,
                                       const PasswordEntry& entry) {
  CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
  row->SetString("profile", profile);
  row->SetDouble("id", static_cast<double>(entry.id));
  row->SetString("origin", entry.origin);
  row->SetList("others", OriginList(entry.other_origins));
  row->SetString("username", entry.username);
  row->SetBool("hasNote", !entry.note.empty());
  row->SetBool("hasTotp", !entry.totp.empty());
  row->SetDouble("created", static_cast<double>(entry.created));
  row->SetDouble("lastUsed", static_cast<double>(entry.last_used));
  row->SetDouble("changed", static_cast<double>(entry.password_changed));
  row->SetDouble("timesUsed", static_cast<double>(entry.times_used));
  return row;
}

void WipeEntry(PasswordEntry& entry) {
  Wipe(entry.password);
  Wipe(entry.note);
  Wipe(entry.totp);
}

}  // namespace

struct PasswordsPageHandler::State {
  std::unique_ptr<PasswordsPageDelegate> delegate;
  std::int64_t unlocked_until = 0;
  // The last deletion, for undo.
  std::vector<std::pair<ProfileId, PasswordEntry>> removed;

  bool Unlocked() const { return Now() < unlocked_until; }

  std::vector<ProfileId> BuiltinProfiles() {
    std::vector<ProfileId> ids;
    for (const PasswordsPageProfile& profile : delegate->Profiles()) {
      if (!profile.bitwarden) {
        ids.push_back(profile.id);
      }
    }
    return ids;
  }

  bool IsBuiltin(const ProfileId& id) {
    for (const PasswordsPageProfile& profile : delegate->Profiles()) {
      if (profile.id == id) {
        return !profile.bitwarden;
      }
    }
    return false;
  }

  void ClearRemoved() {
    for (auto& [profile, entry] : removed) {
      WipeEntry(entry);
    }
    removed.clear();
  }
};

PasswordsPageHandler::PasswordsPageHandler(
    std::unique_ptr<PasswordsPageDelegate> delegate)
    : state_(std::make_shared<State>()) {
  state_->delegate = std::move(delegate);
}

namespace {

// Runs `work` on each profile's store in turn (on the password thread),
// then `done` on the UI thread.
void ForEachProfile(PasswordManager* manager,
                    std::vector<ProfileId> profiles,
                    std::function<void(const ProfileId&, PasswordStore&)> work,
                    std::function<void()> done) {
  if (profiles.empty()) {
    done();
    return;
  }
  ProfileId profile = profiles.front();
  profiles.erase(profiles.begin());
  manager->Run(
      profile, [profile, work](PasswordStore& store) { work(profile, store); },
      [manager, profiles = std::move(profiles), work,
       done = std::move(done)](const std::optional<std::string>&) mutable {
        ForEachProfile(manager, std::move(profiles), std::move(work),
                       std::move(done));
      });
}

}  // namespace

bool PasswordsPageHandler::OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                                   CefRefPtr<CefFrame> frame,
                                   int64_t /*query_id*/,
                                   const CefString& request,
                                   bool persistent,
                                   CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame || InternalPageOf(frame->GetURL().ToString()) != "passwords") {
    return false;
  }
  std::string text = request.ToString();
  CefRefPtr<CefValue> parsed =
      text.size() <= kMaxRequestBytes ? ParseJson(text) : nullptr;
  Wipe(text);
  CefRefPtr<CefDictionaryValue> message =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  std::optional<std::string> type =
      message ? StringField(message, "type", 64) : std::nullopt;
  std::shared_ptr<State> state = state_;
  PasswordManager* manager = state->delegate->Manager();
  if (!type || persistent || !manager) {
    callback->Failure(kBadRequest, "Invalid request.");
    return true;
  }
  auto fail = [&callback](const std::string& why) {
    callback->Failure(kBadRequest, why);
    return true;
  };
  auto locked = [&callback] {
    callback->Failure(kLocked, "Locked.");
    return true;
  };
  std::optional<std::string> profile_field =
      StringField(message, "profile", 64);
  bool profile_ok = profile_field && state->IsBuiltin(*profile_field);
  ProfileId profile = profile_field.value_or("");

  if (*type == "passwords.state") {
    CefRefPtr<CefListValue> profiles = CefListValue::Create();
    for (const PasswordsPageProfile& info : state->delegate->Profiles()) {
      CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
      row->SetString("id", info.id);
      row->SetString("name", info.name);
      row->SetString("color", info.color);
      row->SetBool("bitwarden", info.bitwarden);
      profiles->SetDictionary(profiles->GetSize(), row);
    }
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetList("profiles", profiles);
    answer->SetString("currentProfile", state->delegate->CurrentProfile());
    answer->SetInt("unlockedFor", static_cast<int>(std::max<std::int64_t>(
                                      0, state->unlocked_until - Now())));
    answer->SetBool("breachCheck", state->delegate->BreachCheckEnabled());
    callback->Success(ToJson(answer));
    return true;
  }
  if (*type == "passwords.unlock") {
    if (state->Unlocked()) {
      callback->Success("true");
      return true;
    }
    state->delegate->Authenticate(
        "show and change your saved passwords", [state, callback](bool ok) {
          if (ok) {
            state->unlocked_until = Now() + kUnlockSeconds;
          }
          callback->Success(ok ? "true" : "false");
        });
    return true;
  }
  if (*type == "passwords.lock") {
    state->unlocked_until = 0;
    callback->Success("");
    return true;
  }
  if (*type == "passwords.list") {
    auto rows = std::make_shared<
        std::vector<std::pair<ProfileId, std::vector<PasswordEntry>>>>();
    ForEachProfile(
        manager, state->BuiltinProfiles(),
        [rows](const ProfileId& id, PasswordStore& store) {
          std::vector<PasswordEntry> entries = store.All();
          for (PasswordEntry& entry : entries) {
            // Only whether there is a note or a 2FA secret leaves here.
            bool note = !entry.note.empty();
            bool totp = !entry.totp.empty();
            WipeEntry(entry);
            entry.note = note ? "1" : "";
            entry.totp = totp ? "1" : "";
          }
          rows->emplace_back(id, std::move(entries));
        },
        [rows, callback] {
          CefRefPtr<CefListValue> list = CefListValue::Create();
          for (const auto& [id, entries] : *rows) {
            for (const PasswordEntry& entry : entries) {
              list->SetDictionary(list->GetSize(), EntryRow(id, entry));
            }
          }
          callback->Success(ToJson(list));
        });
    return true;
  }
  std::optional<std::int64_t> id = IdField(message, "id");

  if (*type == "passwords.reveal" || *type == "passwords.copy") {
    std::optional<std::string> field = *type == "passwords.copy"
                                           ? StringField(message, "field", 16)
                                           : std::optional<std::string>("all");
    if (!profile_ok || !id || !field ||
        (*field != "all" && *field != "username" && *field != "password" &&
         *field != "code")) {
      return fail("Invalid request.");
    }
    if (*field != "username" && !state->Unlocked()) {
      return locked();
    }
    auto entry = std::make_shared<std::optional<PasswordEntry>>();
    std::int64_t entry_id = *id;
    const std::string& what = *field;
    manager->Run(
        profile,
        [entry, entry_id](PasswordStore& store) {
          *entry = store.Get(entry_id);
        },
        [entry, what, state,
         callback](const std::optional<std::string>& failed) {
          if (failed || !*entry) {
            callback->Failure(kFailed, "The password no longer exists.");
            return;
          }
          PasswordEntry& found = **entry;
          std::optional<TotpSecret> secret = ParseTotpSecret(found.totp);
          std::int64_t now = Now();
          if (what == "all") {
            CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
            answer->SetString("password", found.password);
            answer->SetString("note", found.note);
            answer->SetString("totp", found.totp);
            answer->SetString("code", secret ? TotpCode(*secret, now) : "");
            answer->SetInt("secondsLeft",
                           secret ? TotpSecondsLeft(*secret, now) : 0);
            std::string json = ToJson(answer);
            callback->Success(json);
            Wipe(json);
          } else {
            state->delegate->CopyToClipboard(what == "username" ? found.username
                                             : what == "password"
                                                 ? found.password
                                             : secret ? TotpCode(*secret, now)
                                                      : std::string());
            callback->Success("");
          }
          WipeEntry(found);
        });
    return true;
  }
  if (*type == "passwords.save") {
    std::optional<std::string> address = StringField(message, "address", 2048);
    std::optional<std::string> username =
        StringField(message, "username", kMaxValueBytes);
    std::optional<std::string> password =
        StringField(message, "password", kMaxValueBytes);
    std::optional<std::string> note =
        StringField(message, "note", kMaxNoteBytes);
    std::optional<std::string> totp =
        StringField(message, "totp", kMaxValueBytes);
    if (!profile_ok || !id || !address || !username || !password || !note ||
        !totp || message->GetType("others") != VTYPE_LIST) {
      return fail("Invalid request.");
    }
    if (*id != 0 && !state->Unlocked()) {
      return locked();
    }
    std::optional<std::string> origin = OriginFromTyped(*address);
    if (!origin) {
      return fail("Enter the site's address, e.g. https://example.com");
    }
    if (password->empty()) {
      return fail("Enter the password.");
    }
    PasswordEntry entry;
    entry.id = *id;
    entry.origin = *origin;
    CefRefPtr<CefListValue> others = message->GetList("others");
    for (std::size_t i = 0; i < others->GetSize() && i < kMaxOrigins; ++i) {
      if (others->GetType(i) != VTYPE_STRING) {
        return fail("Invalid request.");
      }
      std::string typed = others->GetString(i).ToString();
      if (typed.empty()) {
        continue;
      }
      std::optional<std::string> other = OriginFromTyped(typed);
      if (!other) {
        return fail("\"" + typed + "\" is not a site address.");
      }
      if (*other != entry.origin) {
        entry.other_origins.push_back(*other);
      }
    }
    if (!totp->empty()) {
      std::optional<TotpSecret> secret = ParseTotpSecret(*totp);
      if (!secret) {
        return fail(
            "The 2FA secret is not valid: paste an otpauth:// address or the "
            "secret in letters and digits (base32).");
      }
      entry.totp = TotpSecretToUri(*secret);
    }
    entry.username = std::move(*username);
    entry.password = std::move(*password);
    entry.note = std::move(*note);
    auto shared = std::make_shared<PasswordEntry>(std::move(entry));
    auto error = std::make_shared<std::optional<std::string>>();
    manager->Run(
        profile,
        [shared, error](PasswordStore& store) {
          if (shared->id == 0) {
            Result<std::int64_t> added = store.Add(*shared, Now());
            if (!added) {
              *error = added.error().message;
            }
          } else {
            std::optional<PasswordEntry> old = store.Get(shared->id);
            if (!old) {
              *error = "The password no longer exists.";
            } else if (std::optional<Error> failed =
                           store.Update(*shared, Now())) {
              *error = failed->message;
            }
            if (old) {
              WipeEntry(*old);
            }
          }
          WipeEntry(*shared);
        },
        [error, callback](const std::optional<std::string>& failed) {
          if (failed || *error) {
            callback->Failure(kFailed, failed ? *failed : **error);
          } else {
            callback->Success("");
          }
        });
    return true;
  }
  if (*type == "passwords.delete") {
    if (!profile_ok || message->GetType("ids") != VTYPE_LIST ||
        message->GetList("ids")->GetSize() > kMaxIds) {
      return fail("Invalid request.");
    }
    std::vector<std::int64_t> ids;
    CefRefPtr<CefListValue> list = message->GetList("ids");
    for (std::size_t i = 0; i < list->GetSize(); ++i) {
      std::optional<std::int64_t> one = NumberOf(list->GetValue(i));
      if (!one) {
        return fail("Invalid request.");
      }
      ids.push_back(*one);
    }
    state->ClearRemoved();
    auto removed = std::make_shared<std::vector<PasswordEntry>>();
    manager->Run(
        profile,
        [ids, removed](PasswordStore& store) {
          for (std::int64_t one : ids) {
            if (std::optional<PasswordEntry> entry = store.Remove(one)) {
              removed->push_back(std::move(*entry));
            }
          }
        },
        [removed, profile, state, callback](const std::optional<std::string>&) {
          for (PasswordEntry& entry : *removed) {
            state->removed.emplace_back(profile, std::move(entry));
          }
          callback->Success(std::to_string(state->removed.size()));
        });
    return true;
  }
  if (*type == "passwords.undo") {
    auto removed =
        std::make_shared<std::vector<std::pair<ProfileId, PasswordEntry>>>(
            std::move(state->removed));
    state->removed.clear();
    std::vector<ProfileId> profiles;
    for (const auto& [owner, entry] : *removed) {
      if (std::ranges::find(profiles, owner) == profiles.end()) {
        profiles.push_back(owner);
      }
    }
    ForEachProfile(
        manager, profiles,
        [removed](const ProfileId& owner, PasswordStore& store) {
          for (auto& [entry_profile, entry] : *removed) {
            if (entry_profile == owner) {
              store.Restore(entry);
              WipeEntry(entry);
            }
          }
        },
        [callback] { callback->Success(""); });
    return true;
  }
  if (*type == "passwords.never") {
    auto rows = std::make_shared<
        std::vector<std::pair<ProfileId, std::vector<std::string>>>>();
    ForEachProfile(
        manager, state->BuiltinProfiles(),
        [rows](const ProfileId& owner, PasswordStore& store) {
          rows->emplace_back(owner, store.NeverSaveOrigins());
        },
        [rows, callback] {
          CefRefPtr<CefListValue> list = CefListValue::Create();
          for (const auto& [owner, origins] : *rows) {
            for (const std::string& origin : origins) {
              CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
              row->SetString("profile", owner);
              row->SetString("origin", origin);
              list->SetDictionary(list->GetSize(), row);
            }
          }
          callback->Success(ToJson(list));
        });
    return true;
  }
  if (*type == "passwords.never.remove") {
    std::optional<std::string> origin = StringField(message, "origin", 2048);
    if (!profile_ok || !origin) {
      return fail("Invalid request.");
    }
    manager->SetNeverSave(profile, *origin, false);
    callback->Success("");
    return true;
  }
  if (*type == "passwords.import") {
    if (!profile_ok) {
      return fail("Invalid request.");
    }
    state->delegate->OpenCsv([manager, profile, callback](
                                 std::optional<std::string> text,
                                 std::optional<Error> error) {
      if (error) {
        callback->Failure(kFailed, error->message);
        return;
      }
      if (!text) {
        callback->Success("null");  // Cancelled.
        return;
      }
      if (text->size() > kMaxImportBytes) {
        Wipe(*text);
        callback->Failure(kFailed, "The file is too large.");
        return;
      }
      Result<PasswordCsv> csv = ParsePasswordCsv(*text);
      Wipe(*text);
      if (!csv) {
        callback->Failure(kFailed, csv.error().message);
        return;
      }
      auto incoming = std::make_shared<PasswordCsv>(std::move(csv).value());
      auto plan = std::make_shared<PasswordImportPlan>();
      manager->Run(
          profile,
          [incoming, plan](PasswordStore& store) {
            std::vector<PasswordEntry> existing = store.All();
            *plan = PlanPasswordImport(existing, incoming->entries);
            for (PasswordEntry& entry : existing) {
              WipeEntry(entry);
            }
            std::int64_t now = Now();
            for (PasswordEntry& entry : plan->to_add) {
              store.Add(entry, entry.created > 0 ? entry.created : now);
              WipeEntry(entry);
            }
            for (PasswordEntry& entry : incoming->entries) {
              WipeEntry(entry);
            }
          },
          [incoming, plan, callback](const std::optional<std::string>& failed) {
            if (failed) {
              callback->Failure(kFailed, *failed);
              return;
            }
            CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
            answer->SetString("format", incoming->format);
            answer->SetInt("added", static_cast<int>(plan->to_add.size()));
            answer->SetInt("skipped", plan->skipped);
            CefRefPtr<CefListValue> conflicts = CefListValue::Create();
            for (PasswordEntry& entry : plan->conflicts) {
              CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
              row->SetString("origin", entry.origin);
              row->SetString("username", entry.username);
              conflicts->SetDictionary(conflicts->GetSize(), row);
              WipeEntry(entry);
            }
            answer->SetList("conflicts", conflicts);
            CefRefPtr<CefListValue> problems = CefListValue::Create();
            for (const std::string& problem : incoming->problems) {
              problems->SetString(problems->GetSize(), problem);
            }
            answer->SetList("problems", problems);
            callback->Success(ToJson(answer));
          });
    });
    return true;
  }
  if (*type == "passwords.export") {
    if (!profile_ok) {
      return fail("Invalid request.");
    }
    if (!state->Unlocked()) {
      return locked();
    }
    auto csv = std::make_shared<std::string>();
    manager->Run(
        profile,
        [csv](PasswordStore& store) {
          std::vector<PasswordEntry> entries = store.All();
          *csv = WritePasswordCsv(entries);
          for (PasswordEntry& entry : entries) {
            WipeEntry(entry);
          }
        },
        [csv, state, callback](const std::optional<std::string>& failed) {
          if (failed) {
            callback->Failure(kFailed, *failed);
            return;
          }
          state->delegate->SaveCsv(
              std::move(*csv),
              [callback](bool saved, std::optional<Error> error) {
                if (error) {
                  callback->Failure(kFailed, error->message);
                } else {
                  callback->Success(saved ? "true" : "false");
                }
              });
        });
    return true;
  }
  if (*type == "passwords.checkup") {
    if (!state->Unlocked()) {
      return locked();
    }
    struct Item {
      ProfileId profile;
      PasswordEntry entry;
    };
    auto items = std::make_shared<std::vector<Item>>();
    bool breach = state->delegate->BreachCheckEnabled();
    ForEachProfile(
        manager, state->BuiltinProfiles(),
        [items](const ProfileId& owner, PasswordStore& store) {
          for (PasswordEntry& entry : store.All()) {
            Wipe(entry.note);
            Wipe(entry.totp);
            items->push_back({owner, std::move(entry)});
          }
        },
        [items, breach, callback] {
          auto answer = [items, callback](std::vector<std::int64_t> counts) {
            std::vector<std::string> passwords;
            CefRefPtr<CefListValue> rows = CefListValue::Create();
            for (std::size_t i = 0; i < items->size(); ++i) {
              Item& item = (*items)[i];
              CefRefPtr<CefDictionaryValue> row =
                  EntryRow(item.profile, item.entry);
              // For the strength estimate in the page (unlocked).
              row->SetString("password", item.entry.password);
              row->SetDouble("breached", i < counts.size()
                                             ? static_cast<double>(counts[i])
                                             : 0);
              rows->SetDictionary(rows->GetSize(), row);
              passwords.push_back(item.entry.password);
            }
            CefRefPtr<CefListValue> groups = CefListValue::Create();
            for (const std::vector<std::size_t>& group :
                 FindReusedPasswords(passwords)) {
              CefRefPtr<CefListValue> list = CefListValue::Create();
              for (std::size_t index : group) {
                list->SetInt(list->GetSize(), static_cast<int>(index));
              }
              groups->SetList(groups->GetSize(), list);
            }
            for (std::string& password : passwords) {
              Wipe(password);
            }
            for (Item& item : *items) {
              Wipe(item.entry.password);
            }
            CefRefPtr<CefDictionaryValue> result = CefDictionaryValue::Create();
            result->SetList("entries", rows);
            result->SetList("reused", groups);
            std::string json = ToJson(result);
            callback->Success(json);
            Wipe(json);
          };
          if (!breach) {
            answer({});
            return;
          }
          std::vector<std::string> passwords;
          passwords.reserve(items->size());
          for (const Item& item : *items) {
            passwords.push_back(item.entry.password);
          }
          CheckBreaches(std::move(passwords), std::move(answer));
        });
    return true;
  }
  return fail("Unknown request.");
}

}  // namespace brouk
