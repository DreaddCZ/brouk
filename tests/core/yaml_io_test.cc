#include "core/yaml_io.h"

#include <string>

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(YamlIoTest, ParsesMapsScalarsAndQuoting) {
  Result<YamlNode> root = ParseYaml(
      "profile:\n"
      "  name: \"true\"\n"
      "  color: blue\n"
      "list: [a, 'b']\n"
      "empty:\n");
  ASSERT_TRUE(root) << root.error().message;
  ASSERT_EQ(root.value().kind, YamlNode::Kind::kMap);
  const YamlNode* profile = root.value().Find("profile");
  ASSERT_TRUE(profile);
  const YamlNode* name = profile->Find("name");
  ASSERT_TRUE(name);
  EXPECT_EQ(name->scalar, "true");
  EXPECT_FALSE(name->plain);
  EXPECT_EQ(name->line, 2);
  EXPECT_TRUE(profile->Find("color")->plain);
  const YamlNode* list = root.value().Find("list");
  ASSERT_EQ(list->items.size(), 2u);
  EXPECT_FALSE(list->items[1].plain);
  EXPECT_EQ(root.value().Find("empty")->kind, YamlNode::Kind::kNull);
}

TEST(YamlIoTest, EmptyDocumentIsNull) {
  Result<YamlNode> root = ParseYaml("# only a comment\n");
  ASSERT_TRUE(root);
  EXPECT_EQ(root.value().kind, YamlNode::Kind::kNull);
}

TEST(YamlIoTest, ReportsSyntaxErrorsWithLine) {
  Result<YamlNode> root = ParseYaml("a: 1\nb: [unclosed\n");
  ASSERT_FALSE(root);
  EXPECT_NE(root.error().message.find("line"), std::string::npos)
      << root.error().message;
}

TEST(YamlIoTest, RejectsDuplicateKeysAndComplexKeys) {
  EXPECT_FALSE(ParseYaml("a: 1\na: 2\n"));
  EXPECT_FALSE(ParseYaml("? [a, b]\n: 1\n"));
}

TEST(YamlIoTest, RejectsSeveralDocuments) {
  EXPECT_FALSE(ParseYaml("a: 1\n---\nb: 2\n"));
}

TEST(YamlIoTest, RejectsAliasBombsAndDeepNesting) {
  // Each level refers to the previous one ten times: 10^9 values if expanded.
  std::string bomb = "a: &a [x, x, x, x, x, x, x, x, x, x]\n";
  std::string previous = "a";
  for (char level = 'b'; level <= 'j'; ++level) {
    std::string name(1, level);
    bomb += name + ": &" + name + " [";
    for (int i = 0; i < 10; ++i) {
      bomb += (i ? ", *" : "*") + previous;
    }
    bomb += "]\n";
    previous = name;
  }
  EXPECT_FALSE(ParseYaml(bomb));
  EXPECT_FALSE(ParseYaml(std::string(100, '[') + std::string(100, ']')));
}

TEST(YamlIoTest, EmitsReadableYamlThatReadsBack) {
  YamlNode root = YamlNode::Map();
  YamlNode& profile = root.GetOrAdd("profile");
  profile.kind = YamlNode::Kind::kMap;
  profile.GetOrAdd("name") = YamlNode::Scalar("yes: no # not a comment", false);
  profile.GetOrAdd("color") = YamlNode::Scalar("blue", true);
  std::string yaml = EmitYaml(root);
  EXPECT_NE(yaml.find("color: blue"), std::string::npos) << yaml;

  Result<YamlNode> back = ParseYaml(yaml);
  ASSERT_TRUE(back) << back.error().message;
  const YamlNode* name = back.value().Find("profile")->Find("name");
  EXPECT_EQ(name->scalar, "yes: no # not a comment");
  EXPECT_FALSE(name->plain);
}

}  // namespace
}  // namespace brouk
