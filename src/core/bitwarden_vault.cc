#include "core/bitwarden_vault.h"

#include <algorithm>
#include <tuple>
#include <utility>

#include "brouk_adblock.h"
#include "core/crypto.h"
#include "core/json.h"
#include "core/web_origin.h"

namespace brouk {

namespace {

// Bitwarden's item type for logins.
constexpr std::int64_t kLoginType = 1;

// Wipes every string of a parsed document that may hold secrets.
void WipeJson(JsonValue& value) {
  Wipe(value.string);
  for (JsonValue& item : value.items) {
    WipeJson(item);
  }
  for (JsonMember& member : value.members) {
    WipeJson(member.value);
  }
}

// Parses `bw` output that must be a JSON array.
Result<JsonValue> ParseArray(std::string_view json, std::string_view what) {
  Result<JsonValue> value = ParseJsonDocument(json);
  if (!value || value.value().kind != JsonValue::Kind::kArray) {
    return Error{"Bitwarden returned an unexpected list of " +
                 std::string(what) + "."};
  }
  return value;
}

BitwardenUriMatch MatchOf(const JsonValue& uri) {
  const JsonValue* match = uri.Find("match");
  if (match == nullptr || match->kind != JsonValue::Kind::kInteger) {
    return BitwardenUriMatch::kDefault;
  }
  // Bitwarden's UriMatchStrategy numbers.
  switch (match->integer) {
    case 0:
      return BitwardenUriMatch::kDomain;
    case 1:
      return BitwardenUriMatch::kHost;
    case 2:
      return BitwardenUriMatch::kStartsWith;
    case 3:
      return BitwardenUriMatch::kExact;
    case 4:
      return BitwardenUriMatch::kRegularExpression;
    case 5:
      return BitwardenUriMatch::kNever;
    default:
      // Unknown (newer Bitwarden): never matched rather than guessed.
      return BitwardenUriMatch::kNever;
  }
}

// The origin of a URI as Bitwarden reads it: without a scheme, the page's
// scheme is assumed (Bitwarden assumes http, which on an https page would
// only offer the login instead of filling it).
std::optional<std::string> UriOrigin(std::string_view uri,
                                     std::string_view page_origin) {
  if (uri.find("://") == std::string_view::npos) {
    return WebOriginOf(SchemeOfOrigin(page_origin) + "://" + std::string(uri));
  }
  return WebOriginOf(uri);
}

// The origin without its scheme ("example.com:8443").
std::string_view HostAndPort(std::string_view origin) {
  std::size_t separator = origin.find("://");
  return separator == std::string_view::npos ? origin
                                             : origin.substr(separator + 3);
}

bool RegexMatches(const std::string& pattern, std::string_view text) {
  std::string subject(text);
  return brouk_regex_is_match(pattern.c_str(), subject.c_str()) == 1;
}

BitwardenMatchResult MatchUri(const BitwardenUri& uri,
                              std::string_view page_url,
                              std::string_view page_origin) {
  std::string_view text = uri.uri;
  while (!text.empty() && text.front() == ' ') {
    text.remove_prefix(1);
  }
  while (!text.empty() && text.back() == ' ') {
    text.remove_suffix(1);
  }
  if (text.empty()) {
    return {};
  }
  switch (uri.match) {
    case BitwardenUriMatch::kNever:
      return {};
    case BitwardenUriMatch::kExact:
      return {page_url == text ? BitwardenFit::kExact : BitwardenFit::kNone,
              ""};
    case BitwardenUriMatch::kStartsWith:
      return {page_url.starts_with(text) ? BitwardenFit::kExact
                                         : BitwardenFit::kNone,
              ""};
    case BitwardenUriMatch::kRegularExpression:
      return {RegexMatches(std::string(text), page_url) ? BitwardenFit::kExact
                                                        : BitwardenFit::kNone,
              ""};
    case BitwardenUriMatch::kHost:
    case BitwardenUriMatch::kDomain:
    case BitwardenUriMatch::kDefault:
      break;
  }
  std::optional<std::string> origin = UriOrigin(text, page_origin);
  if (!origin) {
    return {};
  }
  if (*origin == page_origin) {
    return {BitwardenFit::kExact, ""};
  }
  bool same_host = HostAndPort(*origin) == HostAndPort(page_origin);
  bool upgrade = SchemeOfOrigin(*origin) == "http" &&
                 SchemeOfOrigin(page_origin) == "https";
  if (uri.match == BitwardenUriMatch::kHost) {
    // Same host and port; an http URI is offered on the https page.
    return {
        same_host && upgrade ? BitwardenFit::kSameSite : BitwardenFit::kNone,
        *origin};
  }
  // The base domain. Other hosts of the site are only offered (008).
  return {IsSameSiteOrUpgrade(*origin, page_origin) ? BitwardenFit::kSameSite
                                                    : BitwardenFit::kNone,
          *origin};
}

// Sets `key` of a JSON object, replacing an existing member.
void SetMember(JsonValue& object, std::string_view key, JsonValue value) {
  for (JsonMember& member : object.members) {
    if (member.key == key) {
      WipeJson(member.value);
      member.value = std::move(value);
      return;
    }
  }
  object.Add(std::string(key), std::move(value));
}

JsonValue StringOrNull(std::string_view text) {
  return text.empty() ? JsonValue{} : JsonValue::String(std::string(text));
}

}  // namespace

std::optional<BitwardenStatus> ParseBitwardenStatus(std::string_view json) {
  Result<JsonValue> value = ParseJsonDocument(json);
  if (!value || value.value().kind != JsonValue::Kind::kObject) {
    return std::nullopt;
  }
  const JsonValue& object = value.value();
  BitwardenStatus status;
  std::string state = object.StringOr("status", "");
  if (state == "unauthenticated") {
    status.state = BitwardenStatus::State::kUnauthenticated;
  } else if (state == "locked") {
    status.state = BitwardenStatus::State::kLocked;
  } else if (state == "unlocked") {
    status.state = BitwardenStatus::State::kUnlocked;
  } else {
    return std::nullopt;
  }
  status.server_url = object.StringOr("serverUrl", "");
  status.user_email = object.StringOr("userEmail", "");
  status.user_id = object.StringOr("userId", "");
  return status;
}

Result<std::vector<BitwardenLogin>> ParseBitwardenLogins(
    std::string_view json) {
  Result<JsonValue> parsed = ParseArray(json, "items");
  if (!parsed) {
    return parsed.error();
  }
  JsonValue& items = parsed.value();
  std::vector<BitwardenLogin> logins;
  for (const JsonValue& item : items.items) {
    if (item.kind != JsonValue::Kind::kObject ||
        item.IntegerOr("type", 0) != kLoginType) {
      continue;
    }
    const JsonValue* deleted = item.Find("deletedDate");
    const JsonValue* login = item.Find("login");
    if ((deleted != nullptr && deleted->kind != JsonValue::Kind::kNull) ||
        login == nullptr || login->kind != JsonValue::Kind::kObject) {
      continue;
    }
    BitwardenLogin entry;
    entry.id = item.StringOr("id", "");
    if (entry.id.empty()) {
      continue;
    }
    entry.name = item.StringOr("name", "");
    entry.username = login->StringOr("username", "");
    entry.password = login->StringOr("password", "");
    entry.totp = login->StringOr("totp", "");
    entry.reprompt = item.IntegerOr("reprompt", 0) != 0;
    entry.folder_id = item.StringOr("folderId", "");
    entry.organization_id = item.StringOr("organizationId", "");
    if (const JsonValue* uris = login->Find("uris");
        uris != nullptr && uris->kind == JsonValue::Kind::kArray) {
      for (const JsonValue& uri : uris->items) {
        if (uri.kind == JsonValue::Kind::kObject) {
          entry.uris.push_back({uri.StringOr("uri", ""), MatchOf(uri)});
        }
      }
    }
    logins.push_back(std::move(entry));
  }
  WipeJson(items);
  return logins;
}

Result<std::vector<BitwardenFolder>> ParseBitwardenFolders(
    std::string_view json) {
  Result<JsonValue> parsed = ParseArray(json, "folders");
  if (!parsed) {
    return parsed.error();
  }
  std::vector<BitwardenFolder> folders;
  for (const JsonValue& folder : parsed.value().items) {
    if (folder.kind == JsonValue::Kind::kObject) {
      folders.push_back(
          {folder.StringOr("id", ""), folder.StringOr("name", "")});
    }
  }
  return folders;
}

Result<std::vector<BitwardenCollection>> ParseBitwardenCollections(
    std::string_view json) {
  Result<JsonValue> parsed = ParseArray(json, "collections");
  if (!parsed) {
    return parsed.error();
  }
  std::vector<BitwardenCollection> collections;
  for (const JsonValue& collection : parsed.value().items) {
    if (collection.kind != JsonValue::Kind::kObject) {
      continue;
    }
    const JsonValue* read_only = collection.Find("readOnly");
    if (read_only != nullptr && read_only->kind == JsonValue::Kind::kBool &&
        read_only->boolean) {
      continue;
    }
    BitwardenCollection entry{collection.StringOr("id", ""),
                              collection.StringOr("organizationId", ""),
                              collection.StringOr("name", "")};
    if (!entry.id.empty() && !entry.organization_id.empty()) {
      collections.push_back(std::move(entry));
    }
  }
  return collections;
}

Result<std::map<std::string, std::string, std::less<>>>
ParseBitwardenOrganizations(std::string_view json) {
  Result<JsonValue> parsed = ParseArray(json, "organizations");
  if (!parsed) {
    return parsed.error();
  }
  std::map<std::string, std::string, std::less<>> organizations;
  for (const JsonValue& organization : parsed.value().items) {
    if (organization.kind == JsonValue::Kind::kObject) {
      std::string id = organization.StringOr("id", "");
      if (!id.empty()) {
        organizations[id] = organization.StringOr("name", "");
      }
    }
  }
  return organizations;
}

BitwardenMatchResult MatchBitwardenLogin(const BitwardenLogin& login,
                                         std::string_view page_url) {
  std::optional<std::string> page_origin = WebOriginOf(page_url);
  if (!page_origin) {
    return {};
  }
  BitwardenMatchResult best;
  for (const BitwardenUri& uri : login.uris) {
    BitwardenMatchResult result = MatchUri(uri, page_url, *page_origin);
    if (result.fit == BitwardenFit::kExact) {
      return result;
    }
    if (result.fit == BitwardenFit::kSameSite &&
        best.fit == BitwardenFit::kNone) {
      best = std::move(result);
    }
  }
  return best;
}

std::string NewBitwardenItemJson(const PasswordEntry& entry,
                                 std::string_view folder_id,
                                 std::string_view organization_id,
                                 std::string_view collection_id) {
  JsonValue uri = JsonValue::Object();
  uri.Add("match", JsonValue{});
  uri.Add("uri", JsonValue::String(entry.origin));
  JsonValue uris = JsonValue::Array();
  uris.items.push_back(std::move(uri));

  JsonValue login = JsonValue::Object();
  login.Add("uris", std::move(uris));
  login.Add("username", StringOrNull(entry.username));
  login.Add("password", JsonValue::String(entry.password));
  login.Add("totp", StringOrNull(entry.totp));

  JsonValue collections;
  if (!organization_id.empty() && !collection_id.empty()) {
    collections = JsonValue::Array();
    collections.items.push_back(JsonValue::String(std::string(collection_id)));
  }
  JsonValue item = JsonValue::Object();
  item.Add("organizationId", StringOrNull(organization_id));
  item.Add("collectionIds", std::move(collections));
  item.Add("folderId", StringOrNull(organization_id.empty() ? folder_id : ""));
  item.Add("type", JsonValue::Integer(kLoginType));
  item.Add("name", JsonValue::String(HostOfOrigin(entry.origin)));
  item.Add("notes", JsonValue{});
  item.Add("favorite", JsonValue::Bool(false));
  item.Add("fields", JsonValue::Array());
  item.Add("login", std::move(login));
  item.Add("reprompt", JsonValue::Integer(0));
  std::string json = WriteJsonDocument(item);
  WipeJson(item);
  return json;
}

Result<std::string> EditedBitwardenItemJson(std::string_view item_json,
                                            const PasswordEntry& entry) {
  Result<JsonValue> parsed = ParseJsonDocument(item_json);
  if (!parsed || parsed.value().kind != JsonValue::Kind::kObject) {
    return Error{"Bitwarden returned an unexpected item."};
  }
  JsonValue& item = parsed.value();
  JsonValue* login = nullptr;
  for (JsonMember& member : item.members) {
    if (member.key == "login" &&
        member.value.kind == JsonValue::Kind::kObject) {
      login = &member.value;
    }
  }
  if (login == nullptr) {
    WipeJson(item);
    return Error{"The Bitwarden item is not a login."};
  }
  SetMember(*login, "username", StringOrNull(entry.username));
  SetMember(*login, "password", JsonValue::String(entry.password));
  SetMember(*login, "totp", StringOrNull(entry.totp));
  std::string json = WriteJsonDocument(item);
  WipeJson(item);
  return json;
}

BitwardenVault::~BitwardenVault() {
  Clear();
}

void BitwardenVault::Replace(std::vector<BitwardenLogin> logins) {
  Clear();
  logins_ = std::move(logins);
  for (const BitwardenLogin& login : logins_) {
    NumberFor(login.id);
  }
}

void BitwardenVault::Clear() {
  for (BitwardenLogin& login : logins_) {
    Wipe(login.password);
    Wipe(login.totp);
  }
  logins_.clear();
}

std::int64_t BitwardenVault::NumberFor(const std::string& item_id) {
  auto [it, added] = numbers_.try_emplace(item_id, next_number_);
  if (added) {
    ++next_number_;
  }
  return it->second;
}

PasswordMatches BitwardenVault::Find(std::string_view page_url) const {
  PasswordMatches matches;
  std::optional<std::string> page_origin = WebOriginOf(page_url);
  if (!page_origin) {
    return matches;
  }
  for (const BitwardenLogin& login : logins_) {
    BitwardenMatchResult result = MatchBitwardenLogin(login, page_url);
    if (result.fit == BitwardenFit::kNone) {
      continue;
    }
    PasswordEntry entry;
    entry.id = numbers_.find(login.id)->second;
    entry.origin =
        result.fit == BitwardenFit::kExact ? *page_origin : result.origin;
    entry.username = login.username;
    entry.password = login.password;
    entry.totp = login.totp;
    entry.reprompt = login.reprompt;
    (result.fit == BitwardenFit::kExact ? matches.exact : matches.same_site)
        .push_back(std::move(entry));
  }
  auto by_username = [](const PasswordEntry& a, const PasswordEntry& b) {
    return std::tie(a.username, a.id) < std::tie(b.username, b.id);
  };
  std::ranges::sort(matches.exact, by_username);
  std::ranges::sort(matches.same_site, by_username);
  return matches;
}

std::optional<std::string> BitwardenVault::ItemId(std::int64_t id) const {
  for (const auto& [item_id, number] : numbers_) {
    if (number == id) {
      return item_id;
    }
  }
  return std::nullopt;
}

std::optional<std::int64_t> BitwardenVault::EntryId(
    std::string_view item_id) const {
  auto it = numbers_.find(item_id);
  if (it == numbers_.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace brouk
