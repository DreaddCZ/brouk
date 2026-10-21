#import "platform/mac/download_notice.h"

namespace {

// A finished notice stays this long unless the user closes it.
constexpr NSTimeInterval kFinishedNoticeSeconds = 30;

}  // namespace

@implementation BRDownloadNotice {
  NSImageView* _icon;
  NSTextField* _label;
  NSButton* _showButton;
  NSButton* _closeButton;
  std::uint32_t _downloadId;
  NSString* _path;
  NSTimer* _hideTimer;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.wantsLayer = YES;
    self.layer.cornerRadius = 8;
    self.layer.borderWidth = 1;
    self.layer.shadowOpacity = 0.2;
    self.layer.shadowRadius = 6;
    self.hidden = YES;
    self.accessibilityRole = NSAccessibilityGroupRole;
    self.accessibilityLabel = @"Download";

    _icon = [NSImageView
        imageViewWithImage:[NSImage
                               imageWithSystemSymbolName:@"arrow.down.circle"
                                accessibilityDescription:nil]];
    _label = [NSTextField labelWithString:@""];
    _label.lineBreakMode = NSLineBreakByTruncatingMiddle;
    _showButton = [NSButton buttonWithTitle:@"Show in Finder"
                                     target:self
                                     action:@selector(showInFinder:)];
    _showButton.controlSize = NSControlSizeSmall;
    _closeButton =
        [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"xmark"
                                            accessibilityDescription:@"Close"]
                           target:self
                           action:@selector(close:)];
    _closeButton.bordered = NO;
    for (NSView* v in @[ _icon, _label, _showButton, _closeButton ]) {
      [self addSubview:v];
    }
  }
  return self;
}

- (BOOL)wantsUpdateLayer {
  return YES;
}

- (void)updateLayer {
  self.layer.backgroundColor = NSColor.windowBackgroundColor.CGColor;
  self.layer.borderColor = NSColor.separatorColor.CGColor;
}

- (void)layout {
  [super layout];
  NSRect b = self.bounds;
  CGFloat midY = NSMidY(b);
  _icon.frame = NSMakeRect(10, midY - 9, 18, 18);
  _closeButton.frame = NSMakeRect(NSMaxX(b) - 28, midY - 10, 20, 20);
  CGFloat showWidth = _showButton.hidden ? 0 : _showButton.fittingSize.width;
  _showButton.frame = NSMakeRect(NSMinX(_closeButton.frame) - 6 - showWidth,
                                 midY - 11, showWidth, 22);
  CGFloat labelX = 34;
  _label.frame =
      NSMakeRect(labelX, midY - 8, NSMinX(_showButton.frame) - 8 - labelX, 16);
}

- (void)updateWithInfo:(const brouk::DownloadInfo&)info {
  using State = brouk::DownloadInfo::State;
  NSString* path = [[NSString alloc] initWithBytes:info.full_path.data()
                                            length:info.full_path.size()
                                          encoding:NSUTF8StringEncoding];
  if (path.length) {
    _path = path;
  }
  _downloadId = info.id;
  NSString* name = _path.lastPathComponent ?: @"file";
  switch (info.state) {
    case State::kInProgress:
      _label.stringValue =
          info.percent >= 0
              ? [NSString stringWithFormat:@"Downloading %@ … %d %%", name,
                                           info.percent]
              : [NSString stringWithFormat:@"Downloading %@ …", name];
      _showButton.hidden = YES;
      break;
    case State::kComplete:
      _label.stringValue = [NSString stringWithFormat:@"%@ downloaded", name];
      _showButton.hidden = NO;
      break;
    case State::kCanceled:
      _label.stringValue =
          [NSString stringWithFormat:@"%@: download canceled", name];
      _showButton.hidden = YES;
      break;
    case State::kFailed:
      _label.stringValue =
          [NSString stringWithFormat:@"%@: download failed", name];
      _showButton.hidden = YES;
      break;
  }
  self.accessibilityValue = _label.stringValue;
  self.hidden = NO;
  self.needsLayout = YES;
  [_hideTimer invalidate];
  _hideTimer = nil;
  if (info.state != State::kInProgress) {
    __weak BRDownloadNotice* weakSelf = self;
    _hideTimer = [NSTimer scheduledTimerWithTimeInterval:kFinishedNoticeSeconds
                                                 repeats:NO
                                                   block:^(NSTimer*) {
                                                     weakSelf.hidden = YES;
                                                   }];
  }
}

- (void)showInFinder:(id)sender {
  if (_path) {
    [NSWorkspace.sharedWorkspace
        activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:_path] ]];
  }
}

- (void)close:(id)sender {
  [_hideTimer invalidate];
  self.hidden = YES;
}

@end
