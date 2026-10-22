#import "platform/mac/screen_share_picker.h"

#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <optional>

namespace {

std::optional<brouk::DesktopSource> SourceFromFilter(SCContentFilter* filter) {
  using Type = brouk::DesktopSource::Type;
  if (@available(macOS 15.2, *)) {
    if (filter.style == SCShareableContentStyleWindow &&
        filter.includedWindows.count > 0) {
      return brouk::DesktopSource{Type::kWindow,
                                  filter.includedWindows.firstObject.windowID};
    }
    if (filter.style == SCShareableContentStyleDisplay &&
        filter.includedDisplays.count > 0) {
      return brouk::DesktopSource{
          Type::kScreen, filter.includedDisplays.firstObject.displayID};
    }
    return std::nullopt;
  }
  // Before macOS 15.2 the filter does not tell which screen was chosen; the
  // picker then only offers screens, and the main screen is shared (CEF's
  // own behaviour).
  if (filter.style == SCShareableContentStyleDisplay) {
    return brouk::DesktopSource{Type::kScreen, CGMainDisplayID()};
  }
  return std::nullopt;
}

}  // namespace

@interface BRScreenSharePicker () <SCContentSharingPickerObserver>
@end

@implementation BRScreenSharePicker {
  BRDesktopSourceCompletion _completion;
}

+ (BRScreenSharePicker*)shared {
  static BRScreenSharePicker* shared = [[BRScreenSharePicker alloc] init];
  return shared;
}

+ (void)pickWithCompletion:(BRDesktopSourceCompletion)completion {
  [[self shared] pickWithCompletion:completion];
}

- (void)pickWithCompletion:(BRDesktopSourceCompletion)completion {
  if (_completion) {
    completion(std::nullopt);
    return;
  }
  _completion = completion;
  SCContentSharingPickerConfiguration* config =
      [[SCContentSharingPickerConfiguration alloc] init];
  config.allowedPickerModes = SCContentSharingPickerModeSingleDisplay;
  if (@available(macOS 15.2, *)) {
    config.allowedPickerModes |= SCContentSharingPickerModeSingleWindow;
  }
  SCContentSharingPicker* picker = SCContentSharingPicker.sharedPicker;
  picker.defaultConfiguration = config;
  [picker addObserver:self];
  picker.active = YES;
  [picker present];
}

// Main thread.
- (void)finishWithSource:(std::optional<brouk::DesktopSource>)source {
  SCContentSharingPicker* picker = SCContentSharingPicker.sharedPicker;
  [picker removeObserver:self];
  picker.active = NO;
  BRDesktopSourceCompletion completion = _completion;
  _completion = nil;
  if (completion) {
    completion(source);
  }
}

#pragma mark - SCContentSharingPickerObserver (any thread)

- (void)contentSharingPicker:(SCContentSharingPicker*)picker
          didCancelForStream:(nullable SCStream*)stream {
  dispatch_async(dispatch_get_main_queue(), ^{
    [self finishWithSource:std::nullopt];
  });
}

- (void)contentSharingPicker:(SCContentSharingPicker*)picker
         didUpdateWithFilter:(SCContentFilter*)filter
                   forStream:(nullable SCStream*)stream {
  std::optional<brouk::DesktopSource> source = SourceFromFilter(filter);
  dispatch_async(dispatch_get_main_queue(), ^{
    [self finishWithSource:source];
  });
}

- (void)contentSharingPickerStartDidFailWithError:(NSError*)error {
  dispatch_async(dispatch_get_main_queue(), ^{
    [self finishWithSource:std::nullopt];
  });
}

@end
