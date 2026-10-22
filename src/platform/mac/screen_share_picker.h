// Shows macOS's own screen sharing picker (ScreenCaptureKit's
// SCContentSharingPicker) and reports the chosen screen or window (012,
// item 3). Chrome shows its own picker; on macOS the system picker gives the
// same choice of screens and windows. Main thread only.

#import <Foundation/Foundation.h>

#include <optional>

#include "core/screen_share.h"

NS_ASSUME_NONNULL_BEGIN

using BRDesktopSourceCompletion =
    void (^)(std::optional<brouk::DesktopSource> source);

@interface BRScreenSharePicker : NSObject

// Shows the picker; `completion` gets the choice, or nullopt when the user
// cancelled or the picker could not be shown. One picker at a time: while
// one is open, another request is answered with nullopt at once.
+ (void)pickWithCompletion:(BRDesktopSourceCompletion)completion;

@end

NS_ASSUME_NONNULL_END
