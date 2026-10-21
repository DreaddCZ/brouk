#import "platform/mac/permission_prompt_view.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "core/permissions.h"

namespace {

constexpr CGFloat kWidth = 340;
constexpr CGFloat kPadding = 16;

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

NSString* SymbolFor(brouk::PermissionKind kind) {
  using K = brouk::PermissionKind;
  switch (kind) {
    case K::kCamera:
    case K::kCameraPanTiltZoom:
      return @"video";
    case K::kMicrophone:
      return @"mic";
    case K::kScreenShare:
      return @"rectangle.on.rectangle";
    case K::kLocation:
      return @"location";
    case K::kNotifications:
      return @"bell";
    case K::kClipboard:
      return @"doc.on.clipboard";
    default:
      return @"lock.open";
  }
}

// What macOS itself blocks, with the System Settings pane to fix it.
struct SystemBlock {
  NSString* text;
  NSString* settings_url;
};

std::vector<SystemBlock> SystemBlocks(
    const std::vector<brouk::PermissionKind>& kinds) {
  using K = brouk::PermissionKind;
  std::vector<SystemBlock> blocks;
  auto denied = [](AVMediaType type) {
    AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:type];
    return status == AVAuthorizationStatusDenied ||
           status == AVAuthorizationStatusRestricted;
  };
  for (K kind : kinds) {
    if ((kind == K::kCamera || kind == K::kCameraPanTiltZoom) &&
        denied(AVMediaTypeVideo)) {
      blocks.push_back({@"macOS does not allow Brouk to use the camera.",
                        @"x-apple.systempreferences:com.apple.preference."
                        @"security?Privacy_Camera"});
    } else if (kind == K::kMicrophone && denied(AVMediaTypeAudio)) {
      blocks.push_back({@"macOS does not allow Brouk to use the microphone.",
                        @"x-apple.systempreferences:com.apple.preference."
                        @"security?Privacy_Microphone"});
    } else if (kind == K::kScreenShare && !CGPreflightScreenCaptureAccess()) {
      blocks.push_back(
          {@"macOS may ask to allow Brouk to record the screen. After "
           @"allowing it, Brouk must be restarted.",
           @"x-apple.systempreferences:com.apple.preference."
           @"security?Privacy_ScreenCapture"});
    }
  }
  return blocks;
}

}  // namespace

@implementation BRPermissionPromptView {
  brouk::PermissionDone _done;
  std::vector<brouk::PermissionButton> _buttons;
  NSMutableArray<NSString*>* _settingsURLs;
}

- (instancetype)initWithRequest:(const brouk::PermissionRequest&)request
                           done:(brouk::PermissionDone)done {
  if ((self = [super initWithFrame:NSZeroRect])) {
    _requestId = request.id;
    _done = std::move(done);
    _buttons = brouk::PermissionButtons(request.kinds);
    _settingsURLs = [NSMutableArray array];
    self.wantsLayer = YES;
    self.layer.cornerRadius = 10;
    self.layer.borderWidth = 1;
    self.layer.shadowOpacity = 0.25;
    self.layer.shadowRadius = 10;
    self.accessibilityRole = NSAccessibilityGroupRole;
    [self build:request];
  }
  return self;
}

- (BOOL)isFlipped {
  return YES;
}

- (BOOL)wantsUpdateLayer {
  return YES;
}

- (void)updateLayer {
  self.layer.backgroundColor = NSColor.windowBackgroundColor.CGColor;
  self.layer.borderColor = NSColor.separatorColor.CGColor;
}

- (void)build:(const brouk::PermissionRequest&)request {
  CGFloat contentWidth = kWidth - 2 * kPadding;
  CGFloat y = kPadding;

  NSString* titleText =
      ToNSString(brouk::PermissionPromptTitle(request.origin));
  self.accessibilityLabel = titleText;
  NSTextField* title = [NSTextField wrappingLabelWithString:titleText];
  title.font = [NSFont boldSystemFontOfSize:13];
  title.preferredMaxLayoutWidth = contentWidth - 24;
  NSSize titleSize = title.fittingSize;
  title.frame = NSMakeRect(kPadding, y, contentWidth - 24, titleSize.height);
  [self addSubview:title];

  NSButton* close =
      [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"xmark"
                                          accessibilityDescription:@"Close"]
                         target:self
                         action:@selector(closeClicked:)];
  close.bordered = NO;
  close.frame = NSMakeRect(kWidth - kPadding - 18, y, 18, 18);
  [self addSubview:close];
  y += titleSize.height + 10;

  for (brouk::PermissionKind kind : request.kinds) {
    NSImageView* icon = [NSImageView
        imageViewWithImage:[NSImage imageWithSystemSymbolName:SymbolFor(kind)
                                     accessibilityDescription:nil]];
    icon.frame = NSMakeRect(kPadding, y, 18, 18);
    [self addSubview:icon];
    NSTextField* line = [NSTextField
        labelWithString:ToNSString(brouk::PermissionDescription(kind))];
    line.frame = NSMakeRect(kPadding + 26, y, contentWidth - 26, 18);
    [self addSubview:line];
    y += 24;
  }

  for (const SystemBlock& block : SystemBlocks(request.kinds)) {
    NSTextField* hint = [NSTextField wrappingLabelWithString:block.text];
    hint.textColor = NSColor.systemOrangeColor;
    hint.preferredMaxLayoutWidth = contentWidth;
    NSSize size = hint.fittingSize;
    hint.frame = NSMakeRect(kPadding, y + 4, contentWidth, size.height);
    [self addSubview:hint];
    y += size.height + 8;
    NSButton* open = [NSButton buttonWithTitle:@"Open System Settings"
                                        target:self
                                        action:@selector(openSettings:)];
    open.tag = static_cast<NSInteger>(_settingsURLs.count);
    [_settingsURLs addObject:block.settings_url];
    open.controlSize = NSControlSizeSmall;
    open.frame = NSMakeRect(kPadding, y, open.fittingSize.width, 22);
    [self addSubview:open];
    y += 30;
  }

  y += 6;
  for (std::size_t i = 0; i < _buttons.size(); ++i) {
    NSButton* button = [NSButton buttonWithTitle:ToNSString(_buttons[i].title)
                                          target:self
                                          action:@selector(choiceClicked:)];
    button.tag = static_cast<NSInteger>(i);
    button.frame = NSMakeRect(kPadding, y, contentWidth, 28);
    [self addSubview:button];
    y += 34;
  }
  self.frame = NSMakeRect(0, 0, kWidth, y + kPadding - 6);
}

- (void)finish:(brouk::PermissionChoice)choice {
  brouk::PermissionDone done = std::move(_done);
  _done = nullptr;
  [self removeFromSuperview];
  if (done) {
    done(choice);
  }
}

- (void)choiceClicked:(NSButton*)sender {
  auto index = static_cast<std::size_t>(sender.tag);
  if (index < _buttons.size()) {
    [self finish:_buttons[index].choice];
  }
}

- (void)closeClicked:(id)sender {
  [self finish:brouk::PermissionChoice::kDismiss];
}

- (void)openSettings:(NSButton*)sender {
  NSString* url = _settingsURLs[static_cast<NSUInteger>(sender.tag)];
  [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:url]];
}

- (void)dismiss {
  _done = nullptr;
  [self removeFromSuperview];
}

@end
