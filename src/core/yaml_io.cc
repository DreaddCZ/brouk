// The only file that uses yaml-cpp. yaml-cpp reports errors by throwing; this
// file is compiled with exceptions and catches them all, so none leaves it
// (CLAUDE.md, "Errors").

#include "core/yaml_io.h"

#include <cstddef>
#include <exception>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace brouk {

namespace {

constexpr std::size_t kMaxBytes = std::size_t{1024} * 1024;
constexpr int kMaxDepth = 32;
constexpr std::size_t kMaxNodes = 100000;

class Converter {
 public:
  // Returns an error message, or an empty string on success.
  std::string Convert(const YAML::Node& in, int depth, YamlNode& out) {
    if (++nodes_ > kMaxNodes) {
      return "the file has too many values";
    }
    if (depth > kMaxDepth) {
      return "the file is nested too deeply";
    }
    out.line = in.Mark().is_null() ? 0 : in.Mark().line + 1;
    switch (in.Type()) {
      case YAML::NodeType::Undefined:
      case YAML::NodeType::Null:
        out.kind = YamlNode::Kind::kNull;
        return {};
      case YAML::NodeType::Scalar:
        out.kind = YamlNode::Kind::kScalar;
        out.scalar = in.Scalar();
        // "?" marks a plain (unquoted, untagged) scalar.
        out.plain = in.Tag() == "?";
        return {};
      case YAML::NodeType::Sequence:
        out.kind = YamlNode::Kind::kSequence;
        for (const YAML::Node& item : in) {
          out.items.emplace_back();
          if (std::string error = Convert(item, depth + 1, out.items.back());
              !error.empty()) {
            return error;
          }
        }
        return {};
      case YAML::NodeType::Map: {
        out.kind = YamlNode::Kind::kMap;
        std::set<std::string> keys;
        for (const auto& pair : in) {
          int line =
              pair.first.Mark().is_null() ? 0 : pair.first.Mark().line + 1;
          if (!pair.first.IsScalar()) {
            return "line " + std::to_string(line) + ": keys must be plain text";
          }
          std::string key = pair.first.Scalar();
          if (!keys.insert(key).second) {
            return "line " + std::to_string(line) + ": \"" + key +
                   "\" appears twice";
          }
          out.entries.push_back({key, YamlNode()});
          if (std::string error =
                  Convert(pair.second, depth + 1, out.entries.back().value);
              !error.empty()) {
            return error;
          }
        }
        return {};
      }
    }
    return "unsupported YAML value";
  }

 private:
  std::size_t nodes_ = 0;
};

void Emit(YAML::Emitter& out, const YamlNode& node) {
  switch (node.kind) {
    case YamlNode::Kind::kNull:
      out << YAML::Null;
      break;
    case YamlNode::Kind::kScalar:
      if (!node.plain) {
        out << YAML::DoubleQuoted;
      }
      out << node.scalar;
      break;
    case YamlNode::Kind::kSequence:
      out << YAML::BeginSeq;
      for (const YamlNode& item : node.items) {
        Emit(out, item);
      }
      out << YAML::EndSeq;
      break;
    case YamlNode::Kind::kMap:
      out << YAML::BeginMap;
      for (const YamlEntry& entry : node.entries) {
        out << YAML::Key << entry.key << YAML::Value;
        Emit(out, entry.value);
      }
      out << YAML::EndMap;
      break;
  }
}

}  // namespace

Result<YamlNode> ParseYaml(std::string_view text) {
  if (text.size() > kMaxBytes) {
    return Error{"the file is too large"};
  }
  try {
    std::vector<YAML::Node> documents = YAML::LoadAll(std::string(text));
    if (documents.size() > 1) {
      return Error{"the file must contain only one YAML document"};
    }
    YamlNode root;
    if (documents.empty()) {
      return root;
    }
    if (std::string error = Converter().Convert(documents.front(), 0, root);
        !error.empty()) {
      return Error{error};
    }
    return root;
  } catch (const YAML::Exception& e) {
    std::string where = e.mark.is_null()
                            ? std::string()
                            : "line " + std::to_string(e.mark.line + 1) + ": ";
    return Error{where + e.msg};
  } catch (const std::exception& e) {
    return Error{e.what()};
  } catch (...) {
    return Error{"the file could not be read as YAML"};
  }
}

std::string EmitYaml(const YamlNode& node) {
  try {
    YAML::Emitter out;
    Emit(out, node);
    if (!out.good()) {
      return {};
    }
    return std::string(out.c_str()) + "\n";
  } catch (...) {
    return {};
  }
}

}  // namespace brouk
