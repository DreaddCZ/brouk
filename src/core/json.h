#ifndef BROUK_CORE_JSON_H_
#define BROUK_CORE_JSON_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/result.h"

namespace brouk {

struct JsonMember;

// A JSON value as plain data, for files the core reads and writes itself
// (bookmarks.json, 011). The core has no CEF, so it cannot use CEF's parser.
// Numbers are kept as integers when they have no fraction or exponent; other
// numbers are read as doubles.
struct JsonValue {
  enum class Kind { kNull, kBool, kInteger, kDouble, kString, kArray, kObject };

  Kind kind = Kind::kNull;
  bool boolean = false;
  std::int64_t integer = 0;
  double number = 0;
  std::string string;
  std::vector<JsonValue> items;     // kArray
  std::vector<JsonMember> members;  // kObject, in order

  static JsonValue Bool(bool value);
  static JsonValue Integer(std::int64_t value);
  static JsonValue String(std::string value);
  static JsonValue Array();
  static JsonValue Object();

  // kObject: the value of `key`, or null if there is none.
  const JsonValue* Find(std::string_view key) const;
  // kObject: adds a member (no check for duplicates).
  void Add(std::string key, JsonValue value);

  // Typed reads of an object's member, with a fallback when it is missing or
  // of another type.
  std::string StringOr(std::string_view key, std::string_view fallback) const;
  std::int64_t IntegerOr(std::string_view key, std::int64_t fallback) const;
};

struct JsonMember {
  std::string key;
  JsonValue value;
};

// RFC 8259 JSON. Nesting deeper than 1000 levels is rejected, so a hostile
// file cannot exhaust the stack.
Result<JsonValue> ParseJson(std::string_view text);

// Pretty-printed with two-space indentation, UTF-8 kept as is; control
// characters, quotes and backslashes escaped.
std::string WriteJson(const JsonValue& value);

}  // namespace brouk

#endif  // BROUK_CORE_JSON_H_
