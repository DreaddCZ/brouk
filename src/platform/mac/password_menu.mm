#import "platform/mac/password_menu.h"

#include <algorithm>
#include <string>
#include <utility>

namespace {

constexpr CGFloat kRowHeight = 40;
constexpr CGFloat kPadding = 4;
constexpr CGFloat kMinWidth = 260;

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

NSString* SymbolFor(brouk::PasswordMenuItem::Kind kind) {
  using Kind = brouk::PasswordMenuItem::Kind;
  switch (kind) {
    case Kind::kLogin:
      return @"key";
    case Kind::kGeneratedPassword:
      return @"wand.and.stars";
    case Kind::kCode:
      return @"lock.rotation";
    case Kind::kReadQrCode:
      return @"qrcode.viewfinder";
    case Kind::kAutofillEntry:
      return @"person.text.rectangle";
    case Kind::kCustomField:
      return @"text.badge.plus";
    case Kind::kClearFilled:
      return @"eraser";
    case Kind::kManageAutofill:
      return @"gearshape";
    case Kind::kManage:
      return @"gearshape";
    case Kind::kWarning:
      return @"exclamationmark.triangle";
    case Kind::kUnlock:
      return @"lock";
  }
  return @"key";
}

bool Selectable(const brouk::PasswordMenuItem& item) {
  return item.kind != brouk::PasswordMenuItem::Kind::kWarning;
}

}  // namespace

@class BRPasswordRowView;

@interface BRPasswordMenu ()
- (void)rowClicked:(BRPasswordRowView*)row;
- (void)rowHovered:(BRPasswordRowView*)row;
@end

@interface BRPasswordRowView : NSView
@property(nonatomic) NSInteger index;
@property(nonatomic) BOOL selectable;
@property(nonatomic) BOOL selected;
@property(nonatomic, weak) BRPasswordMenu* owner;
- (void)showItem:(const brouk::PasswordMenuItem&)item;
@end

@implementation BRPasswordRowView {
  NSImageView* _icon;
  NSTextField* _title;
  NSTextField* _detail;
  NSTrackingArea* _tracking;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _icon = [NSImageView imageViewWithImage:[NSImage new]];
    _icon.contentTintColor = NSColor.secondaryLabelColor;
    _title = [NSTextField labelWithString:@""];
    _title.font = [NSFont systemFontOfSize:13];
    _title.lineBreakMode = NSLineBreakByTruncatingTail;
    _detail = [NSTextField labelWithString:@""];
    _detail.font = [NSFont systemFontOfSize:11];
    _detail.textColor = NSColor.secondaryLabelColor;
    _detail.lineBreakMode = NSLineBreakByTruncatingTail;
    for (NSView* view in @[ _icon, _title, _detail ]) {
      [self addSubview:view];
    }
    self.accessibilityRole = NSAccessibilityRowRole;
  }
  return self;
}

- (void)showItem:(const brouk::PasswordMenuItem&)item {
  using Kind = brouk::PasswordMenuItem::Kind;
  _icon.image = [NSImage imageWithSystemSymbolName:SymbolFor(item.kind)
                          accessibilityDescription:nil];
  _icon.contentTintColor = item.kind == Kind::kWarning
                               ? NSColor.systemOrangeColor
                               : NSColor.secondaryLabelColor;
  _title.stringValue = ToNSString(item.title);
  NSString* detail = ToNSString(item.detail);
  if (item.kind == Kind::kCode) {
    detail = [NSString
        stringWithFormat:@"%@  ·  %d s left", detail, item.seconds_left];
    _detail.font = [NSFont monospacedDigitSystemFontOfSize:12
                                                    weight:NSFontWeightMedium];
  } else if (item.kind == Kind::kGeneratedPassword) {
    _detail.font = [NSFont monospacedSystemFontOfSize:11
                                               weight:NSFontWeightRegular];
  } else {
    _detail.font = [NSFont systemFontOfSize:11];
  }
  _detail.stringValue = detail;
  _title.textColor = item.kind == Kind::kWarning ? NSColor.systemOrangeColor
                                                 : NSColor.labelColor;
  self.selectable = Selectable(item);
  self.accessibilityLabel =
      detail.length > 0
          ? [NSString stringWithFormat:@"%@, %@", _title.stringValue, detail]
          : _title.stringValue;
  self.needsLayout = YES;
}

- (void)setSelected:(BOOL)selected {
  _selected = selected;
  self.accessibilitySelected = selected;
  self.needsDisplay = YES;
}

- (void)layout {
  [super layout];
  NSRect b = self.bounds;
  _icon.frame = NSMakeRect(12, (b.size.height - 16) / 2, 16, 16);
  CGFloat x = 38;
  CGFloat width = b.size.width - x - 10;
  if (_detail.stringValue.length == 0) {
    _title.frame = NSMakeRect(x, (b.size.height - 17) / 2, width, 17);
    _detail.frame = NSZeroRect;
  } else {
    _title.frame = NSMakeRect(x, b.size.height / 2, width, 17);
    _detail.frame = NSMakeRect(x, b.size.height / 2 - 16, width, 15);
  }
}

- (void)drawRect:(NSRect)dirtyRect {
  if (_selected && _selectable) {
    [[NSColor.selectedContentBackgroundColor colorWithAlphaComponent:0.25]
        setFill];
    [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 4, 1)
                                     xRadius:6
                                     yRadius:6] fill];
  }
}

- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  if (_tracking) {
    [self removeTrackingArea:_tracking];
  }
  _tracking = [[NSTrackingArea alloc]
      initWithRect:NSZeroRect
           options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways |
                   NSTrackingInVisibleRect
             owner:self
          userInfo:nil];
  [self addTrackingArea:_tracking];
}

- (void)mouseEntered:(NSEvent*)event {
  [_owner rowHovered:self];
}

- (BOOL)acceptsFirstMouse:(nullable NSEvent*)event {
  return YES;
}

- (void)mouseDown:(NSEvent*)event {
  [_owner rowClicked:self];
}

@end

@implementation BRPasswordMenu {
  NSPanel* _panel;
  NSMutableArray<BRPasswordRowView*>* _rows;
  __weak NSWindow* _parent;
  brouk::PasswordMenuDone _done;
  NSInteger _selected;
}

- (instancetype)init {
  if ((self = [super init])) {
    _rows = [NSMutableArray array];
    _selected = -1;
  }
  return self;
}

- (BOOL)isVisible {
  return _panel.isVisible;
}

- (void)buildPanel {
  _panel =
      [[NSPanel alloc] initWithContentRect:NSZeroRect
                                 styleMask:NSWindowStyleMaskBorderless |
                                           NSWindowStyleMaskNonactivatingPanel
                                   backing:NSBackingStoreBuffered
                                     defer:NO];
  _panel.opaque = NO;
  _panel.backgroundColor = NSColor.clearColor;
  _panel.hasShadow = YES;
  _panel.releasedWhenClosed = NO;
  NSVisualEffectView* background = [[NSVisualEffectView alloc] init];
  background.material = NSVisualEffectMaterialMenu;
  background.state = NSVisualEffectStateActive;
  background.wantsLayer = YES;
  background.layer.cornerRadius = 8;
  background.layer.masksToBounds = YES;
  background.accessibilityRole = NSAccessibilityListRole;
  background.accessibilityLabel = @"Passwords";
  _panel.contentView = background;
}

- (void)showItems:(const std::vector<brouk::PasswordMenuItem>&)items
        belowRect:(NSRect)rect
           inView:(NSView*)view
             done:(brouk::PasswordMenuDone)done {
  [self close];
  if (items.empty() || !view.window) {
    if (done) {
      done(-1);
    }
    return;
  }
  if (!_panel) {
    [self buildPanel];
  }
  _done = std::move(done);
  NSView* content = _panel.contentView;
  for (BRPasswordRowView* row in _rows) {
    [row removeFromSuperview];
  }
  [_rows removeAllObjects];
  NSRect inWindow = [view convertRect:rect toView:nil];
  NSRect onScreen = [view.window convertRectToScreen:inWindow];
  CGFloat width = std::max(onScreen.size.width, kMinWidth);
  CGFloat height =
      static_cast<CGFloat>(items.size()) * kRowHeight + 2 * kPadding;
  NSRect frame = NSMakeRect(NSMinX(onScreen), NSMinY(onScreen) - 2 - height,
                            width, height);
  // Above the field when there is no room below it.
  NSRect visible = view.window.screen.visibleFrame;
  if (NSMinY(frame) < NSMinY(visible)) {
    frame.origin.y = NSMaxY(onScreen) + 2;
  }
  [_panel setFrame:frame display:NO];
  for (std::size_t i = 0; i < items.size(); ++i) {
    BRPasswordRowView* row = [[BRPasswordRowView alloc] init];
    row.owner = self;
    row.index = static_cast<NSInteger>(i);
    row.frame = NSMakeRect(
        0, height - kPadding - (static_cast<CGFloat>(i) + 1) * kRowHeight,
        width, kRowHeight);
    [row showItem:items[i]];
    [content addSubview:row];
    [_rows addObject:row];
  }
  [self select:-1];
  _parent = view.window;
  [view.window addChildWindow:_panel ordered:NSWindowAbove];
  [_panel orderFront:nil];
}

- (void)select:(NSInteger)index {
  _selected = index;
  for (BRPasswordRowView* row in _rows) {
    row.selected = row.index == index;
  }
}

- (void)finish:(NSInteger)index {
  brouk::PasswordMenuDone done = std::move(_done);
  _done = nullptr;
  if (_panel.isVisible) {
    [_parent removeChildWindow:_panel];
    [_panel orderOut:nil];
  }
  _selected = -1;
  if (done) {
    done(static_cast<int>(index));
  }
}

- (void)close {
  [self finish:-1];
}

- (void)move:(NSInteger)step {
  NSInteger count = static_cast<NSInteger>(_rows.count);
  NSInteger index = _selected;
  for (NSInteger tries = 0; tries < count; ++tries) {
    index =
        index < 0 ? (step > 0 ? 0 : count - 1) : (index + step + count) % count;
    if (_rows[static_cast<NSUInteger>(index)].selectable) {
      [self select:index];
      return;
    }
  }
}

- (BOOL)handleKeyCode:(int)windowsKeyCode {
  if (!self.visible) {
    return NO;
  }
  switch (windowsKeyCode) {
    case 0x26:  // Up
      [self move:-1];
      return YES;
    case 0x28:  // Down
      [self move:1];
      return YES;
    case 0x0D:  // Return: only with a row selected, else the page gets it.
      if (_selected >= 0) {
        [self finish:_selected];
        return YES;
      }
      return NO;
    case 0x1B:  // Escape
      [self close];
      return YES;
    default:
      return NO;
  }
}

- (void)rowClicked:(BRPasswordRowView*)row {
  if (row.selectable) {
    [self finish:row.index];
  }
}

- (void)rowHovered:(BRPasswordRowView*)row {
  if (row.selectable) {
    [self select:row.index];
  }
}

@end
