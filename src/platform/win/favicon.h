#ifndef BROUK_PLATFORM_WIN_FAVICON_H_
#define BROUK_PLATFORM_WIN_FAVICON_H_

#include <windows.h>

#include <cstdint>
#include <vector>

namespace brouk::win {

// A tab's icon: a PNG from the page, decoded with the Windows Imaging
// Component into a 32-bit premultiplied bitmap for AlphaBlend. Owns the
// bitmap.
class Favicon {
 public:
  Favicon() = default;
  ~Favicon();
  Favicon(Favicon&& other) noexcept;
  Favicon& operator=(Favicon&& other) noexcept;
  Favicon(const Favicon&) = delete;
  Favicon& operator=(const Favicon&) = delete;

  // Decodes `png` (untrusted, from the page) scaled to `size` x `size`
  // pixels. An empty Favicon if it is not a valid image.
  static Favicon FromPng(const std::vector<std::uint8_t>& png, int size);

  bool empty() const { return bitmap_ == nullptr; }
  // Draws the icon with its alpha channel at (x, y).
  void Draw(HDC dc, int x, int y) const;

 private:
  HBITMAP bitmap_ = nullptr;
  int size_ = 0;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_FAVICON_H_
