// The colours a profile can have (005), as AppKit colours. They follow
// Chrome's tab group colours, with lighter shades in dark mode. Main thread.

#import <Cocoa/Cocoa.h>

#include <string_view>

NS_ASSUME_NONNULL_BEGIN

namespace brouk::mac {

// `name` is one of brouk::ProfileColorNames(); unknown names give grey.
NSColor* ProfileColor(std::string_view name);
// Readable text on top of ProfileColor().
NSColor* ProfileTextColor(std::string_view name);

}  // namespace brouk::mac

NS_ASSUME_NONNULL_END
