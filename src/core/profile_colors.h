#ifndef BROUK_CORE_PROFILE_COLORS_H_
#define BROUK_CORE_PROFILE_COLORS_H_

#include <cstdint>
#include <string_view>

namespace brouk {

// A profile colour's shades (005), Chrome's tab group colours, as 0xRRGGBB:
// for light and for dark mode. `light_text_on_fill`: whether text on the
// colour is white in light mode (yellow and orange are light, so black);
// in dark mode text on any colour is black.
struct ProfileColorShades {
  std::uint32_t light = 0;
  std::uint32_t dark = 0;
  bool light_text_on_fill = true;
};

// The shades of one of ProfileColorNames(); grey for an unknown name.
ProfileColorShades ShadesOfProfileColor(std::string_view name);

}  // namespace brouk

#endif  // BROUK_CORE_PROFILE_COLORS_H_
