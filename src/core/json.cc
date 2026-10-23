#include "core/json.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace brouk {

namespace {

constexpr int kMaxDepth = 1000;

void AppendUtf8(std::string& out, std::uint32_t code) {
  if (code < 0x80) {
    out += static_cast<char>(code);
  } else if (code < 0x800) {
    out += static_cast<char>(0xC0 | (code >> 6));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else if (code < 0x10000) {
    out += static_cast<char>(0xE0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (code >> 18));
    out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  }
}

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  Result<JsonValue> ParseDocument() {
    JsonValue value;
    if (!ParseValue(value, 0)) {
      return Failure();
    }
    SkipSpace();
    if (pos_ != text_.size()) {
      error_ = "unexpected text after the value";
      return Failure();
    }
    return value;
  }

 private:
  Error Failure() const {
    return Error{"invalid JSON at byte " + std::to_string(pos_) + ": " +
                 error_};
  }

  void SkipSpace() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
            text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  bool Consume(std::string_view word) {
    if (text_.substr(pos_, word.size()) == word) {
      pos_ += word.size();
      return true;
    }
    return false;
  }

  bool ParseValue(JsonValue& value, int depth) {
    if (depth > kMaxDepth) {
      error_ = "nested too deeply";
      return false;
    }
    SkipSpace();
    if (pos_ >= text_.size()) {
      error_ = "unexpected end";
      return false;
    }
    char c = text_[pos_];
    if (c == '{') {
      return ParseObject(value, depth);
    }
    if (c == '[') {
      return ParseArray(value, depth);
    }
    if (c == '"') {
      value.kind = JsonValue::Kind::kString;
      return ParseString(value.string);
    }
    if (Consume("true")) {
      value = JsonValue::Bool(true);
      return true;
    }
    if (Consume("false")) {
      value = JsonValue::Bool(false);
      return true;
    }
    if (Consume("null")) {
      value = JsonValue();
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      return ParseNumber(value);
    }
    error_ = "unexpected character";
    return false;
  }

  bool ParseObject(JsonValue& value, int depth) {
    value = JsonValue::Object();
    ++pos_;  // {
    SkipSpace();
    if (Consume("}")) {
      return true;
    }
    while (true) {
      SkipSpace();
      if (pos_ >= text_.size() || text_[pos_] != '"') {
        error_ = "expected a member name";
        return false;
      }
      JsonMember member;
      if (!ParseString(member.key)) {
        return false;
      }
      SkipSpace();
      if (!Consume(":")) {
        error_ = "expected ':'";
        return false;
      }
      if (!ParseValue(member.value, depth + 1)) {
        return false;
      }
      value.members.push_back(std::move(member));
      SkipSpace();
      if (Consume("}")) {
        return true;
      }
      if (!Consume(",")) {
        error_ = "expected ',' or '}'";
        return false;
      }
    }
  }

  bool ParseArray(JsonValue& value, int depth) {
    value = JsonValue::Array();
    ++pos_;  // [
    SkipSpace();
    if (Consume("]")) {
      return true;
    }
    while (true) {
      JsonValue item;
      if (!ParseValue(item, depth + 1)) {
        return false;
      }
      value.items.push_back(std::move(item));
      SkipSpace();
      if (Consume("]")) {
        return true;
      }
      if (!Consume(",")) {
        error_ = "expected ',' or ']'";
        return false;
      }
    }
  }

  bool ParseHex4(std::uint32_t& code) {
    if (pos_ + 4 > text_.size()) {
      error_ = "incomplete \\u escape";
      return false;
    }
    std::string_view digits = text_.substr(pos_, 4);
    auto [end, ec] =
        std::from_chars(digits.data(), digits.data() + digits.size(), code, 16);
    if (ec != std::errc() || end != digits.data() + digits.size()) {
      error_ = "invalid \\u escape";
      return false;
    }
    pos_ += 4;
    return true;
  }

  bool ParseString(std::string& out) {
    ++pos_;  // "
    while (true) {
      if (pos_ >= text_.size()) {
        error_ = "unterminated string";
        return false;
      }
      char c = text_[pos_++];
      if (c == '"') {
        return true;
      }
      if (static_cast<unsigned char>(c) < 0x20) {
        error_ = "control character in a string";
        return false;
      }
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= text_.size()) {
        error_ = "unterminated string";
        return false;
      }
      char escape = text_[pos_++];
      switch (escape) {
        case '"':
        case '\\':
        case '/':
          out += escape;
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u': {
          std::uint32_t code = 0;
          if (!ParseHex4(code)) {
            return false;
          }
          if (code >= 0xD800 && code <= 0xDBFF) {
            std::uint32_t low = 0;
            if (!Consume("\\u") || !ParseHex4(low) || low < 0xDC00 ||
                low > 0xDFFF) {
              error_ = "invalid surrogate pair";
              return false;
            }
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          } else if (code >= 0xDC00 && code <= 0xDFFF) {
            error_ = "invalid surrogate pair";
            return false;
          }
          AppendUtf8(out, code);
          break;
        }
        default:
          error_ = "invalid escape";
          return false;
      }
    }
  }

  bool ParseNumber(JsonValue& value) {
    std::size_t start = pos_;
    bool fraction = false;
    if (text_[pos_] == '-') {
      ++pos_;
    }
    auto digits = [this] {
      std::size_t first = pos_;
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
      return pos_ > first;
    };
    if (!digits()) {
      error_ = "invalid number";
      return false;
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      fraction = true;
      if (!digits()) {
        error_ = "invalid number";
        return false;
      }
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      fraction = true;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      if (!digits()) {
        error_ = "invalid number";
        return false;
      }
    }
    std::string_view number = text_.substr(start, pos_ - start);
    if (!fraction) {
      std::int64_t integer = 0;
      auto [end, ec] = std::from_chars(number.data(),
                                       number.data() + number.size(), integer);
      if (ec == std::errc() && end == number.data() + number.size()) {
        value = JsonValue::Integer(integer);
        return true;
      }
    }
    // Floating-point from_chars is missing on older macOS; a stream with
    // the classic locale reads '.' whatever the user's locale is.
    std::istringstream stream{std::string(number)};
    stream.imbue(std::locale::classic());
    double parsed = 0;
    stream >> parsed;
    if (stream.fail()) {
      error_ = "invalid number";
      return false;
    }
    value.kind = JsonValue::Kind::kDouble;
    value.number = parsed;
    return true;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::string error_;
};

void WriteString(std::string& out, std::string_view text) {
  out += '"';
  for (char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          std::array<char, 8> buffer{};
          std::snprintf(buffer.data(), buffer.size(), "\\u%04x",
                        static_cast<unsigned>(c));
          out += buffer.data();
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

void Indent(std::string& out, int depth) {
  out.append(static_cast<std::size_t>(depth) * 2, ' ');
}

void WriteValue(std::string& out, const JsonValue& value, int depth) {
  switch (value.kind) {
    case JsonValue::Kind::kNull:
      out += "null";
      break;
    case JsonValue::Kind::kBool:
      out += value.boolean ? "true" : "false";
      break;
    case JsonValue::Kind::kInteger:
      out += std::to_string(value.integer);
      break;
    case JsonValue::Kind::kDouble: {
      std::ostringstream stream;
      stream.imbue(std::locale::classic());
      stream.precision(17);
      stream << value.number;
      out += stream.str();
      break;
    }
    case JsonValue::Kind::kString:
      WriteString(out, value.string);
      break;
    case JsonValue::Kind::kArray:
      if (value.items.empty()) {
        out += "[]";
        break;
      }
      out += "[\n";
      for (std::size_t i = 0; i < value.items.size(); ++i) {
        Indent(out, depth + 1);
        WriteValue(out, value.items[i], depth + 1);
        out += i + 1 < value.items.size() ? ",\n" : "\n";
      }
      Indent(out, depth);
      out += ']';
      break;
    case JsonValue::Kind::kObject:
      if (value.members.empty()) {
        out += "{}";
        break;
      }
      out += "{\n";
      for (std::size_t i = 0; i < value.members.size(); ++i) {
        Indent(out, depth + 1);
        WriteString(out, value.members[i].key);
        out += ": ";
        WriteValue(out, value.members[i].value, depth + 1);
        out += i + 1 < value.members.size() ? ",\n" : "\n";
      }
      Indent(out, depth);
      out += '}';
      break;
  }
}

}  // namespace

JsonValue JsonValue::Bool(bool value) {
  JsonValue result;
  result.kind = Kind::kBool;
  result.boolean = value;
  return result;
}

JsonValue JsonValue::Integer(std::int64_t value) {
  JsonValue result;
  result.kind = Kind::kInteger;
  result.integer = value;
  return result;
}

JsonValue JsonValue::String(std::string value) {
  JsonValue result;
  result.kind = Kind::kString;
  result.string = std::move(value);
  return result;
}

JsonValue JsonValue::Array() {
  JsonValue result;
  result.kind = Kind::kArray;
  return result;
}

JsonValue JsonValue::Object() {
  JsonValue result;
  result.kind = Kind::kObject;
  return result;
}

const JsonValue* JsonValue::Find(std::string_view key) const {
  for (const JsonMember& member : members) {
    if (member.key == key) {
      return &member.value;
    }
  }
  return nullptr;
}

void JsonValue::Add(std::string key, JsonValue value) {
  members.push_back({std::move(key), std::move(value)});
}

std::string JsonValue::StringOr(std::string_view key,
                                std::string_view fallback) const {
  const JsonValue* value = Find(key);
  if (value == nullptr || value->kind != Kind::kString) {
    return std::string(fallback);
  }
  return value->string;
}

std::int64_t JsonValue::IntegerOr(std::string_view key,
                                  std::int64_t fallback) const {
  const JsonValue* value = Find(key);
  if (value == nullptr || value->kind != Kind::kInteger) {
    return fallback;
  }
  return value->integer;
}

Result<JsonValue> ParseJson(std::string_view text) {
  return Parser(text).ParseDocument();
}

std::string WriteJson(const JsonValue& value) {
  std::string out;
  WriteValue(out, value, 0);
  out += '\n';
  return out;
}

}  // namespace brouk
