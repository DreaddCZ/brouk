#ifndef BROUK_PLATFORM_WIN_QR_READER_H_
#define BROUK_PLATFORM_WIN_QR_READER_H_

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace brouk::win {

// The texts of the QR codes in a PNG image of a page (008: "Read QR code
// from this page" for a site's 2FA setup), read with zxing-cpp, as Vision
// does on macOS. Empty when there are none or the image cannot be read.
// Takes a moment on a large page: not on the UI thread. Any thread; uses
// COM (initialises it if the thread has not).
std::vector<std::string> ReadQrCodes(std::span<const std::uint8_t> png);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_QR_READER_H_
