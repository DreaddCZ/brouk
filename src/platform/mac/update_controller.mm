#import "platform/mac/update_controller.h"

#import <Sparkle/Sparkle.h>

#include <cstdlib>
#include <string>

namespace {

// Chrome checks shortly after startup, then every few hours (019).
constexpr NSTimeInterval kFirstCheckDelay = 60;

}  // namespace

@interface BRUpdateController () <SPUUpdaterDelegate>
@end

@implementation BRUpdateController {
  SPUStandardUpdaterController* _controller;
  std::string _channel;
  std::string _mode;
  // Installs the downloaded update and relaunches (Restart to Update).
  void (^_installNow)(void);
}

- (instancetype)initWithChannel:(const std::string&)channel
                           mode:(const std::string&)mode {
  if ((self = [super init])) {
    _channel = channel;
    _mode = mode;
    NSDictionary* info = NSBundle.mainBundle.infoDictionary;
    // A test run with its own data folder (smoke tests, 019) must not
    // touch the updater state of the user's own Brouk.
    BOOL testRun = std::getenv("BROUK_DEV_DATA_DIR") != nullptr;
    if ([info[@"SUFeedURL"] length] > 0 && !testRun) {
      _controller =
          [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
                                                        updaterDelegate:self
                                                     userDriverDelegate:nil];
      [self applyMode];
      _controller.updater.sendsSystemProfile = NO;
      [_controller startUpdater];
      __weak BRUpdateController* weakSelf = self;
      dispatch_after(
          dispatch_time(DISPATCH_TIME_NOW,
                        static_cast<int64_t>(kFirstCheckDelay * NSEC_PER_SEC)),
          dispatch_get_main_queue(), ^{
            [weakSelf checkInBackground];
          });
    }
  }
  return self;
}

- (BOOL)available {
  return _controller != nil;
}

- (BOOL)updateReady {
  return _installNow != nil;
}

- (void)setChannel:(const std::string&)channel mode:(const std::string&)mode {
  BOOL channelChanged = channel != _channel;
  _channel = channel;
  _mode = mode;
  if (!_controller) {
    return;
  }
  [self applyMode];
  if (channelChanged) {
    [self checkInBackground];
  }
}

// install: check, download and install by themselves; notify: check and
// ask; off: only when asked (019, updates.mode).
- (void)applyMode {
  SPUUpdater* updater = _controller.updater;
  updater.automaticallyChecksForUpdates = _mode != "off";
  updater.automaticallyDownloadsUpdates = _mode == "install";
}

- (void)checkInBackground {
  if (_controller && _mode != "off" && _controller.updater.canCheckForUpdates) {
    [_controller.updater checkForUpdatesInBackground];
  }
}

- (IBAction)checkForUpdates:(nullable id)sender {
  if (_controller) {
    [_controller checkForUpdates:sender];
    return;
  }
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"Updates are off in this build";
  alert.informativeText =
      @"Only releases update themselves. This copy was built locally.";
  [alert runModal];
}

- (IBAction)restartToUpdate:(nullable id)sender {
  if (_installNow) {
    _installNow();
  }
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
  if (item.action == @selector(restartToUpdate:)) {
    return _installNow != nil;
  }
  if (item.action == @selector(checkForUpdates:)) {
    return !_controller || _controller.updater.canCheckForUpdates;
  }
  return YES;
}

#pragma mark - SPUUpdaterDelegate

// Stable gets stable releases only; beta also gets beta releases.
- (NSSet<NSString*>*)allowedChannelsForUpdater:(SPUUpdater*)updater {
  return _channel == "beta" ? [NSSet setWithObject:@"beta"] : [NSSet set];
}

// Downloaded: installed when Brouk quits, or now with Restart to Update.
- (BOOL)updater:(SPUUpdater*)updater
       willInstallUpdateOnQuit:(SUAppcastItem*)item
    immediateInstallationBlock:(void (^)(void))immediateInstallHandler {
  _installNow = [immediateInstallHandler copy];
  self.restartItem.hidden = NO;
  return YES;
}

@end
