#ifndef BROUK_CORE_YAML_IO_H_
#define BROUK_CORE_YAML_IO_H_

#include <string>
#include <string_view>

#include "core/result.h"
#include "core/yaml_value.h"

namespace brouk {

// Reads one YAML document (spec 004). Settings files are edited by hand, so
// anything can be in them: errors are returned with their line number, and
// documents that are too big or too deeply nested (including through
// aliases) are refused. Map keys must be unique scalars.
Result<YamlNode> ParseYaml(std::string_view text);

// Writes `node` as YAML (block style, ending with a newline). Non-plain
// scalars are double-quoted so they read back as strings.
std::string EmitYaml(const YamlNode& node);

}  // namespace brouk

#endif  // BROUK_CORE_YAML_IO_H_
