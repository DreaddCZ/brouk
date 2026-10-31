#include "core/profile_colors.h"

#include <array>

namespace brouk {

namespace {

struct NamedShades {
  std::string_view name;
  ProfileColorShades shades;
};

// The same values as the macOS app (profile_colors.mm).
constexpr std::array<NamedShades, 9> kShades = {{
    {"grey", {0x5F6368, 0xBDC1C6, true}},
    {"blue", {0x1A73E8, 0x8AB4F8, true}},
    {"red", {0xD93025, 0xF28B82, true}},
    {"yellow", {0xF9AB00, 0xFDD663, false}},
    {"green", {0x1E8E3E, 0x81C995, true}},
    {"pink", {0xD01884, 0xFF8BCB, true}},
    {"purple", {0xA142F4, 0xD7AEFB, true}},
    {"cyan", {0x007B83, 0x78D9EC, true}},
    {"orange", {0xFA903E, 0xFCAD70, false}},
}};

}  // namespace

ProfileColorShades ShadesOfProfileColor(std::string_view name) {
  for (const NamedShades& entry : kShades) {
    if (entry.name == name) {
      return entry.shades;
    }
  }
  return kShades[0].shades;
}

}  // namespace brouk
