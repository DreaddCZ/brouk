#import "platform/mac/downloads_popover.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cmath>
#include <utility>

namespace {

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

NSImage* IconFor(const brouk::DownloadRecord& record) {
  NSString* path = ToNSString(record.path);
  if (path.length > 0 && [NSFileManager.defaultManager fileExistsAtPath:path]) {
    return [NSWorkspace.sharedWorkspace iconForFile:path];
  }
  NSString* extension = ToNSString(record.FileName()).pathExtension;
  UTType* type = [UTType typeWithFilenameExtension:extension] ?: UTTypeData;
  return [NSWorkspace.sharedWorkspace iconForContentType:type];
}

}  // namespace

namespace brouk::mac {

void OpenDownloadedFile(const std::string& path) {
  if (!path.empty()) {
    [NSWorkspace.sharedWorkspace
        openURL:[NSURL fileURLWithPath:ToNSString(path)]];
  }
}

void RevealDownloadedFile(const std::string& path) {
  if (!path.empty()) {
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[
      [NSURL fileURLWithPath:ToNSString(path)]
    ]];
  }
}

bool TrashDownloadedFile(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  return [NSFileManager.defaultManager
        trashItemAtURL:[NSURL fileURLWithPath:ToNSString(path)]
      resultingItemURL:nil
                 error:nil];
}

bool DownloadedFileExists(const std::string& path) {
  return !path.empty() &&
         [NSFileManager.defaultManager fileExistsAtPath:ToNSString(path)];
}

}  // namespace brouk::mac

@implementation BRDownloadsButton

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _progress = NAN;
    self.bordered = NO;
    self.title = @"";
    self.image = [NSImage imageWithSystemSymbolName:@"arrow.down.circle"
                           accessibilityDescription:@"Downloads"];
    self.imagePosition = NSImageOnly;
    self.toolTip = @"Downloads";
    self.accessibilityLabel = @"Downloads";
  }
  return self;
}

- (void)setProgress:(double)progress {
  _progress = progress;
  self.needsDisplay = YES;
  self.accessibilityValue =
      std::isnan(progress) ? nil
      : progress < 0
          ? @"Downloading"
          : [NSString stringWithFormat:@"%d%% downloaded",
                                       static_cast<int>(progress * 100)];
}

- (void)setShowsDot:(BOOL)showsDot {
  _showsDot = showsDot;
  self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];
  NSRect bounds = self.bounds;
  NSPoint center = NSMakePoint(NSMidX(bounds), NSMidY(bounds));
  CGFloat radius = 10;
  if (!std::isnan(_progress)) {
    NSBezierPath* track = [NSBezierPath bezierPath];
    [track appendBezierPathWithArcWithCenter:center
                                      radius:radius
                                  startAngle:0
                                    endAngle:360];
    track.lineWidth = 2;
    [[NSColor.secondaryLabelColor colorWithAlphaComponent:0.25] setStroke];
    [track stroke];
    double fraction = _progress < 0 ? 0.25 : std::clamp(_progress, 0.0, 1.0);
    NSBezierPath* arc = [NSBezierPath bezierPath];
    // Clockwise from the top (the view is not flipped).
    [arc appendBezierPathWithArcWithCenter:center
                                    radius:radius
                                startAngle:90
                                  endAngle:90 - fraction * 360
                                 clockwise:YES];
    arc.lineWidth = 2;
    arc.lineCapStyle = NSLineCapStyleRound;
    [NSColor.controlAccentColor setStroke];
    [arc stroke];
  }
  if (_showsDot) {
    NSRect dot = NSMakeRect(NSMaxX(bounds) - 8, NSMaxY(bounds) - 8, 6, 6);
    [NSColor.controlAccentColor setFill];
    [[NSBezierPath bezierPathWithOvalInRect:dot] fill];
  }
}

@end

@class BRDownloadRowView;

@interface BRDownloadsPopover () <NSPopoverDelegate>
- (void)rowAction:(BRDownloadRowView*)row command:(NSString*)command;
@end

// One download: icon, name, site and status, the profile colour, and the
// actions that suit its state.
@interface BRDownloadRowView : NSView <NSDraggingSource>
@property(nonatomic) brouk::DownloadEntry entry;
@property(nonatomic, weak) BRDownloadsPopover* owner;
@end

@implementation BRDownloadRowView {
  NSImageView* _icon;
  NSTextField* _name;
  NSTextField* _status;
  NSView* _dot;
  NSStackView* _buttons;
  NSPoint _mouseDown;
}

- (instancetype)initWithEntry:(const brouk::DownloadEntry&)entry
                        color:(NSColor*)color
                        owner:(BRDownloadsPopover*)owner {
  if ((self = [super initWithFrame:NSMakeRect(0, 0, 380, 52)])) {
    _entry = entry;
    _owner = owner;
    const brouk::DownloadRecord& record = entry.record;
    _dot = [[NSView alloc] initWithFrame:NSMakeRect(8, 22, 6, 6)];
    _dot.wantsLayer = YES;
    _dot.layer.cornerRadius = 3;
    _dot.layer.backgroundColor = color.CGColor;
    _icon = [NSImageView imageViewWithImage:IconFor(record)];
    _icon.frame = NSMakeRect(20, 10, 32, 32);
    _name = [NSTextField labelWithString:ToNSString(record.FileName())];
    _name.frame = NSMakeRect(60, 27, 200, 17);
    _name.lineBreakMode = NSLineBreakByTruncatingMiddle;
    bool missing = record.state == brouk::DownloadRecord::State::kComplete &&
                   !brouk::mac::DownloadedFileExists(record.path);
    NSString* status =
        missing ? @"Deleted" : ToNSString(brouk::DownloadStatusText(record));
    if (!record.referrer_site.empty()) {
      status =
          [NSString stringWithFormat:@"%@ · %@",
                                     ToNSString(record.referrer_site), status];
    }
    _status = [NSTextField labelWithString:status];
    _status.font = [NSFont systemFontOfSize:11];
    _status.textColor = record.state == brouk::DownloadRecord::State::kFailed
                            ? NSColor.systemRedColor
                            : NSColor.secondaryLabelColor;
    _status.frame = NSMakeRect(60, 9, 200, 15);
    _status.lineBreakMode = NSLineBreakByTruncatingTail;
    _buttons = [[NSStackView alloc] initWithFrame:NSMakeRect(262, 12, 112, 28)];
    _buttons.spacing = 2;
    _buttons.alignment = NSLayoutAttributeCenterY;
    auto add = [&](NSString* symbol, NSString* label, NSString* command) {
      NSButton* button =
          [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:symbol
                                              accessibilityDescription:label]
                             target:self
                             action:@selector(buttonClicked:)];
      button.bordered = NO;
      button.toolTip = label;
      button.identifier = command;
      [_buttons addArrangedSubview:button];
    };
    using State = brouk::DownloadRecord::State;
    switch (record.state) {
      case State::kInProgress:
        add(@"pause.circle", @"Pause", @"pause");
        add(@"xmark.circle", @"Cancel", @"cancel");
        break;
      case State::kPaused:
        add(@"play.circle", @"Resume", @"resume");
        add(@"xmark.circle", @"Cancel", @"cancel");
        break;
      case State::kFailed:
      case State::kCancelled:
        add(@"arrow.clockwise.circle", @"Retry", @"retry");
        break;
      case State::kComplete:
        if (!missing) {
          add(@"magnifyingglass.circle", @"Show in Finder", @"reveal");
        }
        break;
    }
    add(@"ellipsis.circle", @"More", @"more");
    for (NSView* view in @[ _dot, _icon, _name, _status, _buttons ]) {
      [self addSubview:view];
    }
    self.accessibilityRole = NSAccessibilityRowRole;
    self.accessibilityLabel =
        [NSString stringWithFormat:@"%@, %@", _name.stringValue, status];
  }
  return self;
}

- (void)buttonClicked:(NSButton*)sender {
  if ([sender.identifier isEqualToString:@"more"]) {
    [self showMenuAt:sender];
    return;
  }
  [_owner rowAction:self command:sender.identifier];
}

- (void)showMenuAt:(NSView*)view {
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
  bool complete =
      _entry.record.state == brouk::DownloadRecord::State::kComplete &&
      brouk::mac::DownloadedFileExists(_entry.record.path);
  auto item = [&](NSString* title, NSString* command) {
    NSMenuItem* entry = [menu addItemWithTitle:title
                                        action:@selector(menuChosen:)
                                 keyEquivalent:@""];
    entry.target = self;
    entry.representedObject = command;
  };
  if (complete) {
    item(@"Open", @"open");
    item(@"Show in Finder", @"reveal");
  }
  item(@"Copy Download Link", @"copy");
  [menu addItem:NSMenuItem.separatorItem];
  item(@"Remove from List", @"remove");
  if (complete) {
    item(@"Delete File", @"delete");
  }
  [menu popUpMenuPositioningItem:nil
                      atLocation:NSMakePoint(0, NSHeight(view.bounds))
                          inView:view];
}

- (void)menuChosen:(NSMenuItem*)item {
  [_owner rowAction:self command:item.representedObject];
}

- (void)rightMouseDown:(NSEvent*)event {
  [self showMenuAt:self];
}

- (void)mouseDown:(NSEvent*)event {
  _mouseDown = event.locationInWindow;
}

- (void)mouseUp:(NSEvent*)event {
  if (event.clickCount == 1 &&
      _entry.record.state == brouk::DownloadRecord::State::kComplete) {
    [_owner rowAction:self command:@"open"];
  }
}

// A finished file can be dragged to Finder or another app (014).
- (void)mouseDragged:(NSEvent*)event {
  if (_entry.record.state != brouk::DownloadRecord::State::kComplete ||
      !brouk::mac::DownloadedFileExists(_entry.record.path)) {
    return;
  }
  NSPoint now = event.locationInWindow;
  if (std::hypot(now.x - _mouseDown.x, now.y - _mouseDown.y) < 4) {
    return;
  }
  NSURL* url = [NSURL fileURLWithPath:ToNSString(_entry.record.path)];
  NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:url];
  [item setDraggingFrame:_icon.frame contents:_icon.image];
  [self beginDraggingSessionWithItems:@[ item ] event:event source:self];
}

- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
  return NSDragOperationCopy | NSDragOperationGeneric;
}

@end

static BRDownloadsPopover* gShown = nil;

@implementation BRDownloadsPopover {
  NSPopover* _popover;
  NSStackView* _list;
  __weak id<BRDownloadsActions> _actions;
}

+ (BOOL)isShown {
  return gShown != nil;
}

+ (void)showEntries:(std::vector<brouk::DownloadEntry>)entries
         relativeTo:(NSView*)anchor
            actions:(id<BRDownloadsActions>)actions {
  if (gShown) {
    [gShown->_popover close];
  }
  BRDownloadsPopover* popover = [[BRDownloadsPopover alloc] init];
  popover->_actions = actions;
  [popover build:std::move(entries) anchor:anchor];
  gShown = popover;
}

+ (void)updateEntries:(std::vector<brouk::DownloadEntry>)entries {
  [gShown fill:std::move(entries)];
}

- (void)build:(std::vector<brouk::DownloadEntry>)entries
       anchor:(NSView*)anchor {
  NSStackView* stack = [[NSStackView alloc] init];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 6;
  stack.edgeInsets = NSEdgeInsetsMake(12, 8, 12, 8);
  NSTextField* title = [NSTextField labelWithString:@"Downloads"];
  title.font = [NSFont boldSystemFontOfSize:13];
  [stack addArrangedSubview:title];
  _list = [[NSStackView alloc] init];
  _list.orientation = NSUserInterfaceLayoutOrientationVertical;
  _list.spacing = 0;
  NSScrollView* scroll = [[NSScrollView alloc] init];
  scroll.documentView = _list;
  scroll.hasVerticalScroller = YES;
  scroll.drawsBackground = NO;
  [scroll.widthAnchor constraintEqualToConstant:384].active = YES;
  [scroll.heightAnchor constraintEqualToConstant:52 * 6].active = YES;
  _list.translatesAutoresizingMaskIntoConstraints = NO;
  [_list.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor]
      .active = YES;
  [stack addArrangedSubview:scroll];
  NSButton* all = [NSButton buttonWithTitle:@"Show all downloads"
                                     target:self
                                     action:@selector(showAll:)];
  all.bezelStyle = NSBezelStyleInline;
  [stack addArrangedSubview:all];
  [self fill:std::move(entries)];

  NSViewController* controller = [[NSViewController alloc] init];
  controller.view = stack;
  _popover = [[NSPopover alloc] init];
  _popover.contentViewController = controller;
  _popover.behavior = NSPopoverBehaviorTransient;
  _popover.delegate = self;
  [_popover showRelativeToRect:anchor.bounds
                        ofView:anchor
                 preferredEdge:NSRectEdgeMaxY];
}

- (void)fill:(std::vector<brouk::DownloadEntry>)entries {
  for (NSView* view in [_list.arrangedSubviews copy]) {
    [_list removeArrangedSubview:view];
    [view removeFromSuperview];
  }
  id<BRDownloadsActions> actions = _actions;
  if (entries.empty()) {
    [_list addArrangedSubview:[NSTextField
                                  labelWithString:@"No downloads in the last "
                                                  @"24 hours."]];
  }
  for (const brouk::DownloadEntry& entry : entries) {
    NSColor* color =
        [actions colorOfProfile:entry.profile] ?: NSColor.grayColor;
    BRDownloadRowView* row = [[BRDownloadRowView alloc] initWithEntry:entry
                                                                color:color
                                                                owner:self];
    [row.heightAnchor constraintEqualToConstant:52].active = YES;
    [row.widthAnchor constraintEqualToConstant:380].active = YES;
    [_list addArrangedSubview:row];
  }
}

- (void)rowAction:(BRDownloadRowView*)row command:(NSString*)command {
  id<BRDownloadsActions> actions = _actions;
  brouk::DownloadService* service = [actions downloadService];
  const brouk::DownloadEntry& entry = row.entry;
  const brouk::DownloadRecord& record = entry.record;
  if ([command isEqualToString:@"open"]) {
    brouk::mac::OpenDownloadedFile(record.path);
    [_popover close];
  } else if ([command isEqualToString:@"reveal"]) {
    brouk::mac::RevealDownloadedFile(record.path);
  } else if ([command isEqualToString:@"copy"]) {
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    [pasteboard setString:ToNSString(record.url)
                  forType:NSPasteboardTypeString];
  } else if (service && [command isEqualToString:@"pause"]) {
    service->Pause(entry.profile, record.id);
  } else if (service && [command isEqualToString:@"resume"]) {
    service->Resume(entry.profile, record.id);
  } else if (service && [command isEqualToString:@"cancel"]) {
    service->Cancel(entry.profile, record.id);
  } else if ([command isEqualToString:@"retry"]) {
    [actions retryDownload:entry];
  } else if (service && [command isEqualToString:@"remove"]) {
    service->Remove(entry.profile, record.id);
    [_list removeArrangedSubview:row];
    [row removeFromSuperview];
  } else if (service && [command isEqualToString:@"delete"]) {
    brouk::mac::TrashDownloadedFile(record.path);
    service->Remove(entry.profile, record.id);
    [_list removeArrangedSubview:row];
    [row removeFromSuperview];
  }
}

- (void)showAll:(id)sender {
  [_popover close];
  [_actions showAllDownloads];
}

- (void)popoverDidClose:(NSNotification*)notification {
  if (gShown == self) {
    gShown = nil;
  }
}

@end
