// Permission prompt of one tab, at the top left of the page under the address
// bar, as in Chrome (012, item 3). Also tells the user when macOS itself
// blocks Brouk from the camera, microphone or screen. Main thread only.

#import <Cocoa/Cocoa.h>

#include "cef/tab_client.h"

NS_ASSUME_NONNULL_BEGIN

@interface BRPermissionPromptView : NSView

@property(nonatomic, readonly) std::uint64_t requestId;

// `done` is called once with the choice (closing the prompt = dismiss).
- (instancetype)initWithRequest:(const brouk::PermissionRequest&)request
                           done:(brouk::PermissionDone)done;

// Removes the prompt without calling `done`.
- (void)dismiss;

@end

NS_ASSUME_NONNULL_END
