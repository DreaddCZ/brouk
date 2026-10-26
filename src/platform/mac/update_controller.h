// Automatic updates (019) with Sparkle: checks the release feed of this
// build's architecture, downloads in the background and installs when Brouk
// quits, as Chrome does. Only builds made by scripts/release have a feed
// (SUFeedURL in Info.plist); in other builds, and in test runs with their
// own data folder, the updater never starts. Main thread only.

#import <Cocoa/Cocoa.h>

#include <string>

NS_ASSUME_NONNULL_BEGIN

@interface BRUpdateController : NSObject

// Starts the updater when this build has one. `channel` and `mode` are the
// settings updates.channel and updates.mode.
- (instancetype)initWithChannel:(const std::string&)channel
                           mode:(const std::string&)mode
    NS_DESIGNATED_INITIALIZER;
- (instancetype)init NS_UNAVAILABLE;

// Whether this build updates itself at all.
@property(nonatomic, readonly) BOOL available;
// Settings changed.
- (void)setChannel:(const std::string&)channel mode:(const std::string&)mode;

// Brouk → Check for Updates…: the result is shown in a small window.
- (IBAction)checkForUpdates:(nullable id)sender;
// Brouk → Restart to Update: installs the downloaded update and relaunches.
- (IBAction)restartToUpdate:(nullable id)sender;
// An update is downloaded and waits for the next restart.
@property(nonatomic, readonly) BOOL updateReady;
// The Restart to Update menu item, shown once an update is ready.
@property(nonatomic, weak, nullable) NSMenuItem* restartItem;

@end

NS_ASSUME_NONNULL_END
