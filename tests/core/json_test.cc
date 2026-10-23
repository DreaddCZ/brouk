#include "core/json.h"

#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(JsonTest, ParsesValues) {
  Result<JsonValue> json = ParseJson(
      R"({"a": 1, "b": -2.5, "c": "x\ny", "d": [true, false, null], "e": {}})");
  ASSERT_TRUE(json) << json.error().message;
  const JsonValue& value = json.value();
  EXPECT_EQ(value.IntegerOr("a", 0), 1);
  ASSERT_NE(value.Find("b"), nullptr);
  EXPECT_EQ(value.Find("b")->kind, JsonValue::Kind::kDouble);
  EXPECT_DOUBLE_EQ(value.Find("b")->number, -2.5);
  EXPECT_EQ(value.StringOr("c", ""), "x\ny");
  ASSERT_EQ(value.Find("d")->items.size(), 3U);
  EXPECT_TRUE(value.Find("d")->items[0].boolean);
  EXPECT_EQ(value.Find("d")->items[2].kind, JsonValue::Kind::kNull);
  EXPECT_EQ(value.Find("e")->kind, JsonValue::Kind::kObject);
  EXPECT_EQ(value.StringOr("missing", "fallback"), "fallback");
  EXPECT_EQ(value.IntegerOr("c", 7), 7);
}

TEST(JsonTest, DecodesUnicodeEscapes) {
  Result<JsonValue> json = ParseJson(R"("\u010d\ud83d\ude00\/")");
  ASSERT_TRUE(json);
  EXPECT_EQ(json.value().string, "\xC4\x8D\xF0\x9F\x98\x80/");
}

TEST(JsonTest, RejectsInvalidDocuments) {
  for (const char* text :
       {"", "{", "[1,]", "{\"a\" 1}", "\"\\x\"", "\"\\ud800\"", "01x", "tru",
        "\"a\nb\"", "1 2", "{\"a\":1,}", "-"}) {
    EXPECT_FALSE(ParseJson(text)) << text;
  }
}

TEST(JsonTest, LimitsNesting) {
  std::string deep(2000, '[');
  deep += std::string(2000, ']');
  EXPECT_FALSE(ParseJson(deep));
  std::string fine(500, '[');
  fine += std::string(500, ']');
  EXPECT_TRUE(ParseJson(fine));
}

TEST(JsonTest, WritesAndReadsBack) {
  JsonValue value = JsonValue::Object();
  value.Add("name", JsonValue::String("a \"quote\"\\ \t\x01 č"));
  value.Add("n", JsonValue::Integer(-9007199254740993));
  JsonValue list = JsonValue::Array();
  list.items.push_back(JsonValue::Bool(true));
  list.items.push_back(JsonValue());
  value.Add("list", list);
  value.Add("empty", JsonValue::Array());
  std::string text = WriteJson(value);
  EXPECT_NE(text.find("\\u0001"), std::string::npos);
  Result<JsonValue> back = ParseJson(text);
  ASSERT_TRUE(back) << back.error().message;
  EXPECT_EQ(back.value().StringOr("name", ""), "a \"quote\"\\ \t\x01 č");
  EXPECT_EQ(back.value().IntegerOr("n", 0), -9007199254740993);
  EXPECT_EQ(back.value().Find("list")->items.size(), 2U);
  EXPECT_EQ(WriteJson(back.value()), text);
}

}  // namespace
}  // namespace brouk
