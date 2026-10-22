#ifndef BROUK_CORE_YAML_VALUE_H_
#define BROUK_CORE_YAML_VALUE_H_

#include <string>
#include <string_view>
#include <vector>

namespace brouk {

struct YamlEntry;

// A parsed YAML document as plain data (spec 004). The rest of the code works
// with this, never with the YAML library.
struct YamlNode {
  enum class Kind { kNull, kScalar, kSequence, kMap };

  Kind kind = Kind::kNull;
  // kScalar: the text. `plain` is false for quoted scalars, so "true" (a
  // string) can be told apart from true (a boolean).
  std::string scalar;
  bool plain = true;
  std::vector<YamlNode> items;     // kSequence
  std::vector<YamlEntry> entries;  // kMap, in file order
  int line = 0;                    // 1-based line in the file, 0 if unknown

  static YamlNode Scalar(std::string text, bool plain);
  static YamlNode Map();

  // kMap: the value for `key`, or null if there is none.
  const YamlNode* Find(std::string_view key) const;
  // kMap: the value for `key`, added (as null) if missing.
  YamlNode& GetOrAdd(std::string_view key);
};

struct YamlEntry {
  std::string key;
  YamlNode value;
};

}  // namespace brouk

#endif  // BROUK_CORE_YAML_VALUE_H_
