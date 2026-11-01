#include "core/utf8_path.h"

#include <cstddef>

namespace brouk {

namespace {

// Well-formed UTF-8 (RFC 3629): no overlong forms, surrogates or code
// points above U+10FFFF, which the Windows conversion would refuse.
bool IsValidUtf8(std::string_view text) {
  std::size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i]);
    std::size_t length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (lead < 0x80) {
      ++i;
      continue;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
      length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      length = 3;
      if (lead == 0xE0) {
        low = 0xA0;
      } else if (lead == 0xED) {
        high = 0x9F;
      }
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      length = 4;
      if (lead == 0xF0) {
        low = 0x90;
      } else if (lead == 0xF4) {
        high = 0x8F;
      }
    } else {
      return false;
    }
    if (text.size() - i < length) {
      return false;
    }
    for (std::size_t k = 1; k < length; ++k) {
      const auto byte = static_cast<unsigned char>(text[i + k]);
      const unsigned char min = k == 1 ? low : 0x80;
      const unsigned char max = k == 1 ? high : 0xBF;
      if (byte < min || byte > max) {
        return false;
      }
    }
    i += length;
  }
  return true;
}

}  // namespace

std::string PathToUtf8(const std::filesystem::path& path) {
  const std::u8string text = path.u8string();
  return {text.begin(), text.end()};
}

std::filesystem::path PathFromUtf8(std::string_view text) {
  if (!IsValidUtf8(text)) {
    return {};
  }
  return {std::u8string(text.begin(), text.end())};
}

}  // namespace brouk
