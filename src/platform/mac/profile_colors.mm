#import "platform/mac/profile_colors.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace brouk::mac {

namespace {

struct Shades {
  std::string_view name;
  std::uint32_t light;  // 0xRRGGBB
  std::uint32_t dark;
};

// Chrome's tab group colours.
constexpr std::array<Shades, 9> kShades = {{
    {"grey", 0x5F6368, 0xBDC1C6},
    {"blue", 0x1A73E8, 0x8AB4F8},
    {"red", 0xD93025, 0xF28B82},
    {"yellow", 0xF9AB00, 0xFDD663},
    {"green", 0x1E8E3E, 0x81C995},
    {"pink", 0xD01884, 0xFF8BCB},
    {"purple", 0xA142F4, 0xD7AEFB},
    {"cyan", 0x007B83, 0x78D9EC},
    {"orange", 0xFA903E, 0xFCAD70},
}};

NSColor* FromRgb(std::uint32_t rgb) {
  return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0
                             green:((rgb >> 8) & 0xFF) / 255.0
                              blue:(rgb & 0xFF) / 255.0
                             alpha:1];
}

const Shades& ShadesFor(std::string_view name) {
  for (const Shades& shades : kShades) {
    if (shades.name == name) {
      return shades;
    }
  }
  return kShades[0];
}

BOOL IsDark(NSAppearance* appearance) {
  return [appearance bestMatchFromAppearancesWithNames:@[
           NSAppearanceNameAqua, NSAppearanceNameDarkAqua
         ]] == NSAppearanceNameDarkAqua;
}

}  // namespace

NSColor* ProfileColor(std::string_view name) {
  const Shades& shades = ShadesFor(name);
  std::uint32_t light = shades.light;
  std::uint32_t dark = shades.dark;
  return [NSColor colorWithName:nil
                dynamicProvider:^NSColor*(NSAppearance* appearance) {
                  return FromRgb(IsDark(appearance) ? dark : light);
                }];
}

NSColor* ProfileTextColor(std::string_view name) {
  // Yellow and orange are light in both modes; the dark-mode shades of all
  // colours are light.
  BOOL light_fill = name == "yellow" || name == "orange";
  return [NSColor colorWithName:nil
                dynamicProvider:^NSColor*(NSAppearance* appearance) {
                  return (light_fill || IsDark(appearance))
                             ? NSColor.blackColor
                             : NSColor.whiteColor;
                }];
}

NSImage* ProfileDot(std::string_view name) {
  NSColor* fill = ProfileColor(name);
  return [NSImage
       imageWithSize:NSMakeSize(12, 12)
             flipped:NO
      drawingHandler:^BOOL(NSRect rect) {
        [fill setFill];
        [[NSBezierPath bezierPathWithOvalInRect:NSInsetRect(rect, 1, 1)] fill];
        return YES;
      }];
}

}  // namespace brouk::mac
