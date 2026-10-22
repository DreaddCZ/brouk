#include "core/yaml_value.h"

#include <string>
#include <string_view>
#include <utility>

namespace brouk {

YamlNode YamlNode::Scalar(std::string text, bool plain) {
  YamlNode node;
  node.kind = Kind::kScalar;
  node.scalar = std::move(text);
  node.plain = plain;
  return node;
}

YamlNode YamlNode::Map() {
  YamlNode node;
  node.kind = Kind::kMap;
  return node;
}

const YamlNode* YamlNode::Find(std::string_view key) const {
  for (const YamlEntry& entry : entries) {
    if (entry.key == key) {
      return &entry.value;
    }
  }
  return nullptr;
}

YamlNode& YamlNode::GetOrAdd(std::string_view key) {
  for (YamlEntry& entry : entries) {
    if (entry.key == key) {
      return entry.value;
    }
  }
  entries.push_back({std::string(key), YamlNode()});
  return entries.back().value;
}

}  // namespace brouk
