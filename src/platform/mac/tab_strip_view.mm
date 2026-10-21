#import "platform/mac/tab_strip_view.h"

#import "platform/mac/browser_tab.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr CGFloat kMaxTabWidth = 220;
constexpr CGFloat kMinTabWidth = 44;
constexpr CGFloat kTabGap = 1;
constexpr CGFloat kNewTabButtonWidth = 32;
constexpr CGFloat kCloseButtonSize = 16;
constexpr CGFloat kIconSize = 16;
constexpr CGFloat kTabPadding = 8;
// Horizontal mouse movement that turns a click into a drag.
constexpr CGFloat kDragThreshold = 4;

NSColor* StripBackgroundColor() {
  return [NSColor.windowBackgroundColor
      blendedColorWithFraction:0.10
                       ofColor:NSColor.labelColor];
}

NSColor* ActiveTabColor() {
  return NSColor.windowBackgroundColor;
}

}  // namespace

@class BRTabView;

@interface BRTabStripView ()
- (void)tabViewMouseDown:(BRTabView*)tabView event:(NSEvent*)event;
- (void)tabViewClose:(BRTabView*)tabView;
@end

// One tab: icon (or spinner), title, and a close button behind a divider so a
// drag never starts on the close button by accident (003).
@interface BRTabView : NSView
@property(nonatomic) brouk::TabId tabId;
@property(nonatomic) BOOL active;
@property(nonatomic, weak) BRTabStripView* strip;
- (void)updateFromTab:(BRTab*)tab;
@end

@implementation BRTabView {
  NSImageView* _icon;
  NSProgressIndicator* _spinner;
  NSTextField* _label;
  NSButton* _close;
  BOOL _hovered;
  NSTrackingArea* _tracking;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _icon = [[NSImageView alloc] initWithFrame:NSZeroRect];
    _icon.imageScaling = NSImageScaleProportionallyUpOrDown;
    [self addSubview:_icon];

    _spinner = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
    _spinner.style = NSProgressIndicatorStyleSpinning;
    _spinner.controlSize = NSControlSizeSmall;
    _spinner.displayedWhenStopped = NO;
    [self addSubview:_spinner];

    _label = [NSTextField labelWithString:@""];
    _label.lineBreakMode = NSLineBreakByTruncatingTail;
    _label.font = [NSFont systemFontOfSize:12];
    _label.cell.usesSingleLineMode = YES;
    [self addSubview:_label];

    _close = [NSButton
        buttonWithImage:[NSImage imageWithSystemSymbolName:@"xmark"
                                  accessibilityDescription:@"Close tab"]
                 target:self
                 action:@selector(closeClicked:)];
    _close.bordered = NO;
    _close.imagePosition = NSImageOnly;
    _close.symbolConfiguration = [NSImageSymbolConfiguration
        configurationWithPointSize:9
                            weight:NSFontWeightSemibold];
    _close.contentTintColor = NSColor.secondaryLabelColor;
    _close.toolTip = @"Close tab (⌘W)";
    [self addSubview:_close];

    self.accessibilityRole = NSAccessibilityRadioButtonRole;
  }
  return self;
}

- (BOOL)isFlipped {
  return YES;
}

- (BOOL)mouseDownCanMoveWindow {
  return NO;
}

- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  if (_tracking) {
    [self removeTrackingArea:_tracking];
  }
  _tracking = [[NSTrackingArea alloc]
      initWithRect:NSZeroRect
           options:NSTrackingMouseEnteredAndExited |
                   NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
             owner:self
          userInfo:nil];
  [self addTrackingArea:_tracking];
}

- (void)mouseEntered:(NSEvent*)event {
  _hovered = YES;
  self.needsDisplay = YES;
}

- (void)mouseExited:(NSEvent*)event {
  _hovered = NO;
  self.needsDisplay = YES;
}

- (void)setActive:(BOOL)active {
  _active = active;
  self.accessibilityValue = @(active);
  _label.textColor = active ? NSColor.labelColor : NSColor.secondaryLabelColor;
  self.needsDisplay = YES;
}

- (void)updateFromTab:(BRTab*)tab {
  NSString* title = tab.title.length ? tab.title : tab.url;
  if (title.length == 0) {
    title = @"New Tab";
  }
  _label.stringValue = title;
  self.toolTip = title;
  self.accessibilityLabel = title;
  _icon.image = tab.favicon
                    ?: [NSImage imageWithSystemSymbolName:@"globe"
                                 accessibilityDescription:nil];
  if (tab.isLoading) {
    _icon.hidden = YES;
    [_spinner startAnimation:nil];
  } else {
    [_spinner stopAnimation:nil];
    _icon.hidden = NO;
  }
}

- (void)layout {
  [super layout];
  NSRect b = self.bounds;
  CGFloat midY = NSMidY(b);
  CGFloat closeX = NSMaxX(b) - kTabPadding - kCloseButtonSize;
  _close.frame = NSMakeRect(closeX, midY - kCloseButtonSize / 2,
                            kCloseButtonSize, kCloseButtonSize);
  // Narrow tabs show only icon and close button.
  BOOL showIcon =
      b.size.width >= kTabPadding * 2 + kIconSize + kCloseButtonSize + 6;
  NSRect iconRect =
      NSMakeRect(kTabPadding, midY - kIconSize / 2, kIconSize, kIconSize);
  _icon.frame = iconRect;
  _spinner.frame = iconRect;
  _icon.alphaValue = showIcon ? 1 : 0;
  CGFloat labelX = NSMaxX(iconRect) + 6;
  CGFloat labelWidth = closeX - 6 - labelX;
  _label.hidden = labelWidth < 12;
  _label.frame =
      NSMakeRect(labelX, midY - 8, std::max<CGFloat>(labelWidth, 0), 16);
}

- (void)drawRect:(NSRect)dirtyRect {
  NSRect b = NSInsetRect(self.bounds, 0, 0);
  if (_active || _hovered) {
    NSColor* fill = _active ? ActiveTabColor()
                            : [ActiveTabColor() colorWithAlphaComponent:0.5];
    NSRect r =
        NSMakeRect(b.origin.x, b.origin.y + 4, b.size.width, b.size.height - 4);
    NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:r
                                                         xRadius:7
                                                         yRadius:7];
    [fill setFill];
    [path fill];
    // Square off the bottom so the active tab joins the toolbar.
    if (_active) {
      NSRectFill(NSMakeRect(r.origin.x, NSMaxY(r) - 8, r.size.width, 8));
    }
  }
  // Divider between the drag area and the close button.
  NSRect closeFrame = _close.frame;
  [[NSColor.separatorColor colorWithAlphaComponent:0.6] setFill];
  NSRectFill(NSMakeRect(NSMinX(closeFrame) - 4, NSMidY(b) - 6, 1, 12));
}

- (void)mouseDown:(NSEvent*)event {
  [_strip tabViewMouseDown:self event:event];
}

- (void)otherMouseUp:(NSEvent*)event {
  // Middle click closes the tab, as in Chrome.
  if (event.buttonNumber == 2) {
    [_strip tabViewClose:self];
  }
}

- (void)closeClicked:(id)sender {
  [_strip tabViewClose:self];
}

- (BOOL)accessibilityPerformPress {
  [_strip.delegate tabStripDidSelectTab:_tabId];
  return YES;
}

@end

@implementation BRTabStripView {
  NSMutableArray<BRTabView*>* _tabViews;
  NSButton* _newTabButton;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _tabViews = [NSMutableArray array];
    _newTabButton =
        [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"plus"
                                            accessibilityDescription:@"New tab"]
                           target:self
                           action:@selector(newTabClicked:)];
    _newTabButton.bordered = NO;
    _newTabButton.toolTip = @"New tab (⌘T)";
    [self addSubview:_newTabButton];
    self.accessibilityRole = NSAccessibilityTabGroupRole;
    self.accessibilityLabel = @"Tabs";
  }
  return self;
}

- (BOOL)isFlipped {
  return YES;
}

// Fill only our bounds: since macOS 14 views do not clip to their bounds and
// dirtyRect may extend beyond them.
- (void)drawRect:(NSRect)dirtyRect {
  [StripBackgroundColor() setFill];
  NSRectFill(self.bounds);
}

- (void)mouseDown:(NSEvent*)event {
  // Empty strip area behaves like a title bar.
  if (event.clickCount == 2) {
    [self.window performZoom:nil];
  } else {
    [self.window performWindowDragWithEvent:event];
  }
}

- (void)setTabs:(NSArray<BRTab*>*)tabs activeTabId:(brouk::TabId)activeTabId {
  // Reuse views by id so hover and animation state survive updates.
  NSMutableArray<BRTabView*>* views = [NSMutableArray array];
  for (BRTab* tab in tabs) {
    BRTabView* view = nil;
    for (BRTabView* existing in _tabViews) {
      if (existing.tabId == tab.tabId) {
        view = existing;
        break;
      }
    }
    if (!view) {
      view = [[BRTabView alloc] initWithFrame:NSZeroRect];
      view.tabId = tab.tabId;
      view.strip = self;
      [self addSubview:view];
    }
    view.active = tab.tabId == activeTabId;
    [view updateFromTab:tab];
    [views addObject:view];
  }
  for (BRTabView* old in _tabViews) {
    if (![views containsObject:old]) {
      [old removeFromSuperview];
    }
  }
  _tabViews = views;
  self.needsLayout = YES;
}

- (void)updateTab:(BRTab*)tab {
  for (BRTabView* view in _tabViews) {
    if (view.tabId == tab.tabId) {
      [view updateFromTab:tab];
    }
  }
}

- (CGFloat)tabWidth {
  CGFloat available =
      self.bounds.size.width - _leadingInset - kNewTabButtonWidth - 8;
  NSUInteger count = std::max<NSUInteger>(_tabViews.count, 1);
  CGFloat width = std::floor(available / count) - kTabGap;
  return std::clamp(width, kMinTabWidth, kMaxTabWidth);
}

- (void)layout {
  [super layout];
  CGFloat width = [self tabWidth];
  CGFloat height = self.bounds.size.height;
  CGFloat x = _leadingInset;
  for (BRTabView* view in _tabViews) {
    view.frame = NSMakeRect(x, 0, width, height);
    x += width + kTabGap;
  }
  _newTabButton.frame =
      NSMakeRect(x + 2, (height - 24) / 2 + 2, kNewTabButtonWidth - 4, 24);
}

#pragma mark - Tab interaction

- (void)tabViewClose:(BRTabView*)tabView {
  [_delegate tabStripDidCloseTab:tabView.tabId];
}

// Click selects; horizontal drag reorders (the dragged tab follows the mouse
// and the drop position is the slot under its center).
- (void)tabViewMouseDown:(BRTabView*)tabView event:(NSEvent*)event {
  [_delegate tabStripDidSelectTab:tabView.tabId];

  NSPoint start = [self convertPoint:event.locationInWindow fromView:nil];
  NSRect startFrame = tabView.frame;
  BOOL dragging = NO;
  CGFloat width = [self tabWidth];
  while (true) {
    NSEvent* next =
        [self.window nextEventMatchingMask:NSEventMaskLeftMouseDragged |
                                           NSEventMaskLeftMouseUp];
    NSPoint p = [self convertPoint:next.locationInWindow fromView:nil];
    CGFloat dx = p.x - start.x;
    if (next.type == NSEventTypeLeftMouseUp) {
      break;
    }
    if (!dragging && std::fabs(dx) < kDragThreshold) {
      continue;
    }
    dragging = YES;
    NSRect f = startFrame;
    f.origin.x =
        std::clamp(startFrame.origin.x + dx, _leadingInset,
                   _leadingInset + (width + kTabGap) * (_tabViews.count - 1));
    tabView.frame = f;
    [tabView.superview addSubview:tabView
                       positioned:NSWindowAbove
                       relativeTo:nil];
  }
  if (dragging) {
    CGFloat center = NSMidX(tabView.frame) - _leadingInset;
    NSInteger index = static_cast<NSInteger>(center / (width + kTabGap));
    index = std::clamp<NSInteger>(index, 0,
                                  static_cast<NSInteger>(_tabViews.count) - 1);
    [_delegate tabStripDidMoveTab:tabView.tabId
                          toIndex:static_cast<NSUInteger>(index)];
    self.needsLayout = YES;
  }
}

- (void)newTabClicked:(id)sender {
  [_delegate tabStripDidRequestNewTab];
}

@end
