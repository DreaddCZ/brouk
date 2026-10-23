// Watches the data folder for changes to settings files (and request rules
// files, 006) (004: changes made
// in the files apply while the browser runs). Chromium's own data under
// web/ changes all the time and is ignored. Main thread only.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface BRSettingsWatcher : NSObject

// Calls `changed` on the main thread, at most a few times per second, after
// a settings file in `directory` (or below it, except web/) changed.
- (instancetype)initWithDirectory:(NSString*)directory
                          changed:(void (^)(void))changed;
- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
