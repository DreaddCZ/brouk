#include "core/password_profile.h"

#include <charconv>
#include <optional>
#include <string>

#include "core/json.h"

namespace brouk {

namespace {

constexpr std::string_view kAssociated = "brouk-profile-passwords";

YamlNode Plain(std::string text) {
  return YamlNode::Scalar(std::move(text), true);
}

std::optional<std::uint64_t> Number(const YamlNode& node,
                                    std::string_view key) {
  const YamlNode* value = node.Find(key);
  std::uint64_t number = 0;
  if (!value || value->kind != YamlNode::Kind::kScalar ||
      std::from_chars(value->scalar.data(),
                      value->scalar.data() + value->scalar.size(), number)
              .ec != std::errc()) {
    return std::nullopt;
  }
  return number;
}

std::optional<std::string> Text(const YamlNode& node, std::string_view key) {
  const YamlNode* value = node.Find(key);
  if (!value || value->kind != YamlNode::Kind::kScalar) {
    return std::nullopt;
  }
  return value->scalar;
}

JsonValue EntryToJson(const PasswordEntry& entry) {
  JsonValue json = JsonValue::Object();
  json.Add("origin", JsonValue::String(entry.origin));
  JsonValue others = JsonValue::Array();
  for (const std::string& origin : entry.other_origins) {
    others.items.push_back(JsonValue::String(origin));
  }
  json.Add("other_origins", std::move(others));
  json.Add("username", JsonValue::String(entry.username));
  json.Add("password", JsonValue::String(entry.password));
  json.Add("note", JsonValue::String(entry.note));
  json.Add("totp", JsonValue::String(entry.totp));
  json.Add("created", JsonValue::Integer(entry.created));
  json.Add("password_changed", JsonValue::Integer(entry.password_changed));
  return json;
}

}  // namespace

YamlNode ExportProfilePasswords(const std::vector<PasswordEntry>& entries,
                                std::string_view export_password,
                                PasswordKdfCost cost) {
  JsonValue list = JsonValue::Array();
  for (const PasswordEntry& entry : entries) {
    list.items.push_back(EntryToJson(entry));
  }
  std::string json = WriteJsonDocument(list);
  std::string salt = RandomBytes(kPasswordSaltSize);
  std::optional<SecretKey> key =
      DeriveKeyFromPassword(export_password, salt, cost);
  YamlNode node = YamlNode::Map();
  if (!key) {
    Wipe(json);
    return {};
  }
  node.GetOrAdd("kdf") = Plain("argon2id");
  node.GetOrAdd("ops") = Plain(std::to_string(cost.ops));
  node.GetOrAdd("memory") = Plain(std::to_string(cost.memory));
  node.GetOrAdd("salt") = Plain(Base64Encode(salt));
  node.GetOrAdd("data") = Plain(Base64Encode(Encrypt(*key, json, kAssociated)));
  Wipe(json);
  return node;
}

Result<std::vector<PasswordEntry>> ImportProfilePasswords(
    const YamlNode& node,
    std::string_view export_password) {
  const Error damaged{"The passwords in this file are damaged."};
  if (node.kind != YamlNode::Kind::kMap || Text(node, "kdf") != "argon2id") {
    return damaged;
  }
  std::optional<std::uint64_t> ops = Number(node, "ops");
  std::optional<std::uint64_t> memory = Number(node, "memory");
  std::optional<std::string> salt_text = Text(node, "salt");
  std::optional<std::string> data_text = Text(node, "data");
  std::optional<std::string> salt =
      salt_text ? Base64Decode(*salt_text) : std::nullopt;
  std::optional<std::string> data =
      data_text ? Base64Decode(*data_text) : std::nullopt;
  // Bounds keep a hostile file from asking for absurd work or memory.
  if (!ops || !memory || !salt || !data || *ops > 20 ||
      *memory > std::uint64_t{1024} * 1024 * 1024) {
    return damaged;
  }
  std::optional<SecretKey> key = DeriveKeyFromPassword(
      export_password, *salt, {*ops, static_cast<std::size_t>(*memory)});
  if (!key) {
    return damaged;
  }
  std::optional<std::string> json = Decrypt(*key, *data, kAssociated);
  if (!json) {
    return Error{"The export password is wrong."};
  }
  Result<JsonValue> parsed = ParseJsonDocument(*json);
  Wipe(*json);
  if (!parsed || parsed.value().kind != JsonValue::Kind::kArray) {
    return damaged;
  }
  std::vector<PasswordEntry> entries;
  for (const JsonValue& item : parsed.value().items) {
    if (item.kind != JsonValue::Kind::kObject) {
      continue;
    }
    PasswordEntry entry;
    entry.origin = item.StringOr("origin", "");
    if (const JsonValue* others = item.Find("other_origins");
        others && others->kind == JsonValue::Kind::kArray) {
      for (const JsonValue& other : others->items) {
        if (other.kind == JsonValue::Kind::kString) {
          entry.other_origins.push_back(other.string);
        }
      }
    }
    entry.username = item.StringOr("username", "");
    entry.password = item.StringOr("password", "");
    entry.note = item.StringOr("note", "");
    entry.totp = item.StringOr("totp", "");
    entry.created = item.IntegerOr("created", 0);
    entry.password_changed = item.IntegerOr("password_changed", 0);
    if (!entry.origin.empty() && !entry.password.empty()) {
      entries.push_back(std::move(entry));
    }
  }
  // The parsed document still holds the plain passwords.
  for (JsonValue& item : parsed.value().items) {
    for (JsonMember& member : item.members) {
      Wipe(member.value.string);
    }
  }
  return entries;
}

}  // namespace brouk
