#import "platform/mac/tab_strip_view.h"

#import "platform/mac/browser_tab.h"
#import "platform/mac/profile_colors.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "core/tab_strip_layout.h"

namespace {

constexpr CGFloat kTabGap = 1;
constexpr CGFloat kMoreButtonWidth = 40;
constexpr CGFloat kNewTabButtonWidth = 26;
constexpr CGFloat kGroupGap = 8;
constexpr CGFloat kLabelGap = 4;
constexpr CGFloat kMaxLabelWidth = 160;
constexpr CGFloat kGroupLineHeight = 2;
constexpr CGFloat kCloseButtonSize = 16;
constexpr CGFloat kIconSize = 16;
constexpr CGFloat kTabPadding = 8;
// The narrowest tab: icon and close button (003).
constexpr CGFloat kIconOnlyTabWidth =
    kTabPadding * 2 + kIconSize + kCloseButtonSize + 6;
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
@class BRGroupLabelView;

@interface BRTabStripView ()
- (void)tabViewMouseDown:(BRTabView*)tabView event:(NSEvent*)event;
- (void)groupLabelClicked:(BRGroupLabelView*)label;
- (void)tabViewClose:(BRTabView*)tabView;
- (void)tabViewToggleMute:(BRTabView*)tabView;
- (nullable NSMenu*)menuForTabView:(BRTabView*)tabView;
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
  NSButton* _audio;  // Speaker: shown while playing or muted; click mutes.
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

    _audio = [NSButton buttonWithImage:[NSImage new]
                                target:self
                                action:@selector(audioClicked:)];
    _audio.bordered = NO;
    _audio.imagePosition = NSImageOnly;
    _audio.symbolConfiguration = [NSImageSymbolConfiguration
        configurationWithPointSize:10
                            weight:NSFontWeightMedium];
    _audio.contentTintColor = NSColor.secondaryLabelColor;
    _audio.hidden = YES;
    [self addSubview:_audio];

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
  if (title.length == 0 || [title isEqualToString:@"about:blank"]) {
    title = @"New Tab";
  }
  _label.stringValue = title;
  self.toolTip = title;
  self.accessibilityLabel = title;
  _icon.image = tab.favicon
                    ?: [NSImage imageWithSystemSymbolName:@"globe"
                                 accessibilityDescription:nil];
  BOOL showAudio = tab.isAudible || tab.isMuted;
  if (showAudio) {
    BOOL muted = tab.isMuted;
    _audio.image =
        [NSImage imageWithSystemSymbolName:muted ? @"speaker.slash.fill"
                                                 : @"speaker.wave.2.fill"
                  accessibilityDescription:muted ? @"Unmute tab" : @"Mute tab"];
    _audio.toolTip = muted ? @"Unmute tab" : @"Mute tab";
  }
  if (_audio.hidden == showAudio) {
    _audio.hidden = !showAudio;
    self.needsLayout = YES;
  }
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
  BOOL showIcon = b.size.width >= kIconOnlyTabWidth;
  NSRect iconRect =
      NSMakeRect(kTabPadding, midY - kIconSize / 2, kIconSize, kIconSize);
  _icon.frame = iconRect;
  _spinner.frame = iconRect;
  _icon.alphaValue = showIcon ? 1 : 0;
  CGFloat labelX = NSMaxX(iconRect) + 6;
  CGFloat labelEnd = closeX - 6;
  if (!_audio.hidden) {
    _audio.frame = NSMakeRect(closeX - 8 - 16, midY - 8, 16, 16);
    labelEnd = NSMinX(_audio.frame) - 2;
  }
  CGFloat labelWidth = labelEnd - labelX;
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

- (void)audioClicked:(id)sender {
  [_strip tabViewToggleMute:self];
}

- (nullable NSMenu*)menuForEvent:(NSEvent*)event {
  return [_strip menuForTabView:self];
}

- (BOOL)accessibilityPerformPress {
  [_strip.delegate tabStripDidSelectTab:_tabId];
  return YES;
}

@end

@implementation BRTabGroupInfo
@end

// A group's label: a chip in the profile's colour with its name and number
// of tabs. Clicking it collapses or expands the group (003).
@interface BRGroupLabelView : NSView
@property(nonatomic, copy) NSString* profileId;
@property(nonatomic, weak) BRTabStripView* strip;
// Collapsed by the layout for lack of space (clicking switches to it).
@property(nonatomic) BOOL autoCollapsed;
- (void)updateWithGroup:(BRTabGroupInfo*)group;
- (CGFloat)preferredWidth;
@end

@implementation BRGroupLabelView {
  NSString* _colorName;
  NSString* _name;
  NSUInteger _count;
  NSString* _text;
  BOOL _collapsed;  // By the user.
}

- (BOOL)isFlipped {
  return YES;
}

- (BOOL)mouseDownCanMoveWindow {
  return NO;
}

- (NSDictionary*)textAttributes {
  return @{
    NSFontAttributeName : [NSFont systemFontOfSize:11
                                            weight:NSFontWeightSemibold],
    NSForegroundColorAttributeName :
        brouk::mac::ProfileTextColor(_colorName.UTF8String ?: ""),
  };
}

- (void)updateWithGroup:(BRTabGroupInfo*)group {
  _colorName = group.colorName;
  _collapsed = group.collapsed;
  _name = group.name;
  _count = group.tabs.count;
  _text = _count > 0
              ? [NSString stringWithFormat:@"%@  %lu", _name,
                                           static_cast<unsigned long>(_count)]
              : _name;
  self.accessibilityRole = NSAccessibilityButtonRole;
  [self updateDescriptions];
  self.needsDisplay = YES;
}

- (void)setAutoCollapsed:(BOOL)autoCollapsed {
  if (_autoCollapsed != autoCollapsed) {
    _autoCollapsed = autoCollapsed;
    [self updateDescriptions];
    self.needsDisplay = YES;
  }
}

- (BOOL)shownCollapsed {
  return _collapsed || _autoCollapsed;
}

- (void)updateDescriptions {
  NSString* tabs = _count == 1 ? @"tab" : @"tabs";
  unsigned long count = _count;
  NSString* action = _autoCollapsed ? @"show its tabs"
                     : _collapsed   ? @"expand"
                                    : @"collapse";
  self.toolTip = [NSString
      stringWithFormat:@"%@: %lu %@. Click to %@.", _name, count, tabs, action];
  self.accessibilityLabel = [NSString
      stringWithFormat:@"Profile %@, %lu %@, %@", _name, count, tabs,
                       [self shownCollapsed] ? @"collapsed" : @"expanded"];
}

- (CGFloat)preferredWidth {
  CGFloat text = [_text sizeWithAttributes:[self textAttributes]].width;
  return std::min(std::ceil(text) + 16, kMaxLabelWidth);
}

- (void)drawRect:(NSRect)dirtyRect {
  NSRect chip = NSInsetRect(self.bounds, 0, (self.bounds.size.height - 20) / 2);
  chip.origin.y += 2;
  NSColor* color = brouk::mac::ProfileColor(_colorName.UTF8String ?: "");
  NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:chip
                                                       xRadius:6
                                                       yRadius:6];
  BOOL collapsed = [self shownCollapsed];
  if (collapsed) {
    // Collapsed: outlined, so it reads as "closed" at a glance.
    [color setStroke];
    path.lineWidth = 1.5;
    [[color colorWithAlphaComponent:0.15] setFill];
    [path fill];
    [path stroke];
  } else {
    [color setFill];
    [path fill];
  }
  NSMutableDictionary* attributes = [[self textAttributes] mutableCopy];
  if (collapsed) {
    attributes[NSForegroundColorAttributeName] = NSColor.labelColor;
  }
  NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
  style.lineBreakMode = NSLineBreakByTruncatingTail;
  style.alignment = NSTextAlignmentCenter;
  attributes[NSParagraphStyleAttributeName] = style;
  NSRect textRect = NSInsetRect(chip, 6, 0);
  textRect.origin.y += (chip.size.height - 14) / 2;
  textRect.size.height = 14;
  [_text drawInRect:textRect withAttributes:attributes];
}

- (void)mouseDown:(NSEvent*)event {
  [_strip groupLabelClicked:self];
}

- (BOOL)accessibilityPerformPress {
  [_strip groupLabelClicked:self];
  return YES;
}

@end

// The coloured line under a group's tabs.
@interface BRGroupLineView : NSView
@property(nonatomic, copy) NSString* colorName;
@end

@implementation BRGroupLineView
- (void)drawRect:(NSRect)dirtyRect {
  [brouk::mac::ProfileColor(_colorName.UTF8String ?: "") setFill];
  NSRectFill(self.bounds);
}
@end

@implementation BRTabStripView {
  NSArray<BRTabGroupInfo*>* _groups;
  NSMutableArray<BRTabView*>* _tabViews;  // All visible tabs, left to right.
  NSMutableDictionary<NSString*, BRGroupLabelView*>* _labels;
  NSMutableDictionary<NSString*, NSButton*>* _plusButtons;
  NSMutableDictionary<NSString*, BRGroupLineView*>* _lines;
  // "N more" buttons of groups that hide some tabs.
  NSMutableDictionary<NSString*, NSButton*>* _moreButtons;
  NSMutableDictionary<NSNumber*, BRTab*>* _tabsById;
  brouk::TabId _activeTabId;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _groups = @[];
    _tabViews = [NSMutableArray array];
    _labels = [NSMutableDictionary dictionary];
    _plusButtons = [NSMutableDictionary dictionary];
    _lines = [NSMutableDictionary dictionary];
    _moreButtons = [NSMutableDictionary dictionary];
    _tabsById = [NSMutableDictionary dictionary];
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

- (BRTabView*)tabViewForTab:(BRTab*)tab {
  for (BRTabView* existing in _tabViews) {
    if (existing.tabId == tab.tabId) {
      return existing;
    }
  }
  BRTabView* view = [[BRTabView alloc] initWithFrame:NSZeroRect];
  view.tabId = tab.tabId;
  view.strip = self;
  [self addSubview:view];
  return view;
}

- (void)setGroups:(NSArray<BRTabGroupInfo*>*)groups
      activeTabId:(brouk::TabId)activeTabId {
  _groups = [groups copy];
  _activeTabId = activeTabId;
  [_tabsById removeAllObjects];
  // Reuse views so hover and animation state survive updates.
  NSMutableArray<BRTabView*>* views = [NSMutableArray array];
  NSMutableSet<NSString*>* profiles = [NSMutableSet set];
  for (BRTabGroupInfo* group in groups) {
    [profiles addObject:group.profileId];
    BRGroupLabelView* label = _labels[group.profileId];
    if (!label) {
      label = [[BRGroupLabelView alloc] initWithFrame:NSZeroRect];
      label.profileId = group.profileId;
      label.strip = self;
      [self addSubview:label];
      _labels[group.profileId] = label;
    }
    [label updateWithGroup:group];

    NSButton* plus = _plusButtons[group.profileId];
    if (!plus) {
      plus = [NSButton
          buttonWithImage:[NSImage imageWithSystemSymbolName:@"plus"
                                    accessibilityDescription:@"New tab"]
                   target:self
                   action:@selector(plusClicked:)];
      plus.bordered = NO;
      plus.identifier = group.profileId;
      [self addSubview:plus];
      _plusButtons[group.profileId] = plus;
    }
    plus.toolTip = [NSString stringWithFormat:@"New tab in %@", group.name];
    plus.accessibilityLabel = plus.toolTip;
    plus.contentTintColor =
        brouk::mac::ProfileColor(group.colorName.UTF8String ?: "");

    BRGroupLineView* line = _lines[group.profileId];
    if (!line) {
      line = [[BRGroupLineView alloc] initWithFrame:NSZeroRect];
      _lines[group.profileId] = line;
    }
    line.colorName = group.colorName;
    line.needsDisplay = YES;

    for (BRTab* tab in group.tabs) {
      _tabsById[@(tab.tabId)] = tab;
    }
    if (group.collapsed) {
      continue;
    }
    for (BRTab* tab in group.tabs) {
      BRTabView* view = [self tabViewForTab:tab];
      view.active = tab.tabId == activeTabId;
      [view updateFromTab:tab];
      [views addObject:view];
    }
  }
  for (BRTabView* old in _tabViews) {
    if (![views containsObject:old]) {
      [old removeFromSuperview];
    }
  }
  _tabViews = views;
  for (NSString* profile in [_labels.allKeys copy]) {
    if (![profiles containsObject:profile]) {
      [_labels[profile] removeFromSuperview];
      [_plusButtons[profile] removeFromSuperview];
      [_lines[profile] removeFromSuperview];
      [_moreButtons[profile] removeFromSuperview];
      [_labels removeObjectForKey:profile];
      [_plusButtons removeObjectForKey:profile];
      [_lines removeObjectForKey:profile];
      [_moreButtons removeObjectForKey:profile];
    }
  }
  self.needsLayout = YES;
}

- (void)updateTab:(BRTab*)tab {
  for (BRTabView* view in _tabViews) {
    if (view.tabId == tab.tabId) {
      [view updateFromTab:tab];
    }
  }
}

// Labels and "+" buttons keep their size; the tabs share the rest, as
// brouk::LayoutTabStrip decides.
- (std::vector<brouk::TabStripGroupLayout>)computeLayout {
  brouk::TabStripLayoutInput input;
  input.width = self.bounds.size.width - _leadingInset;
  input.metrics.tab_gap = kTabGap;
  input.metrics.min_tab_width = kIconOnlyTabWidth;
  input.metrics.more_button_width = kMoreButtonWidth;
  for (NSUInteger g = 0; g < _groups.count; ++g) {
    BRTabGroupInfo* group = _groups[g];
    input.groups.push_back(
        {.fixed_width = [_labels[group.profileId] preferredWidth] + kLabelGap +
                        kNewTabButtonWidth + kGroupGap,
         .tab_count = group.tabs.count,
         .pinned_count = group.pinnedCount,
         .collapsed = static_cast<bool>(group.collapsed)});
    for (NSUInteger t = 0; t < group.tabs.count; ++t) {
      if (group.tabs[t].tabId == _activeTabId) {
        input.active_group = g;
        input.active_tab = t;
      }
    }
  }
  return brouk::LayoutTabStrip(input);
}

- (NSButton*)moreButtonForGroup:(BRTabGroupInfo*)group {
  NSButton* more = _moreButtons[group.profileId];
  if (!more) {
    more = [NSButton buttonWithTitle:@""
                              target:self
                              action:@selector(moreClicked:)];
    more.bordered = NO;
    more.font = [NSFont systemFontOfSize:11 weight:NSFontWeightSemibold];
    more.contentTintColor = NSColor.secondaryLabelColor;
    more.identifier = group.profileId;
    [self addSubview:more];
    _moreButtons[group.profileId] = more;
  }
  return more;
}

- (void)layout {
  [super layout];
  std::vector<brouk::TabStripGroupLayout> layouts = [self computeLayout];
  CGFloat height = self.bounds.size.height;
  CGFloat x = _leadingInset;
  NSUInteger next = 0;
  for (NSUInteger g = 0; g < _groups.count; ++g) {
    BRTabGroupInfo* group = _groups[g];
    const brouk::TabStripGroupLayout& layout = layouts[g];
    BRGroupLabelView* label = _labels[group.profileId];
    label.autoCollapsed = layout.auto_collapsed;
    CGFloat labelWidth = [label preferredWidth];
    label.frame = NSMakeRect(x, 0, labelWidth, height);
    CGFloat groupStart = x;
    x += labelWidth + kLabelGap;
    if (!group.collapsed) {
      // Views exist for all tabs of groups the user did not collapse.
      for (NSUInteger t = 0; t < group.tabs.count; ++t) {
        BRTabView* view = _tabViews[next++];
        CGFloat width = layout.tab_widths[t];
        view.hidden = width <= 0;
        if (width > 0) {
          view.frame = NSMakeRect(x, 0, width, height);
          x += width + kTabGap;
        }
      }
    }
    NSButton* more = _moreButtons[group.profileId];
    if (layout.hidden_count > 0) {
      more = [self moreButtonForGroup:group];
      unsigned long hidden = layout.hidden_count;
      more.title = [NSString stringWithFormat:@"+%lu", hidden];
      more.toolTip = [NSString stringWithFormat:@"%lu more %@", hidden,
                                                hidden == 1 ? @"tab" : @"tabs"];
      more.accessibilityLabel = more.toolTip;
      more.hidden = NO;
      more.frame =
          NSMakeRect(x, (height - 24) / 2 + 2, kMoreButtonWidth - 4, 24);
      x += kMoreButtonWidth;
    } else {
      more.hidden = YES;
    }
    _plusButtons[group.profileId].frame =
        NSMakeRect(x, (height - 24) / 2 + 2, kNewTabButtonWidth - 4, 24);
    x += kNewTabButtonWidth;
    // The group's line runs under its label, tabs and "+".
    BRGroupLineView* line = _lines[group.profileId];
    line.frame = NSMakeRect(groupStart, height - kGroupLineHeight,
                            x - groupStart - 4, kGroupLineHeight);
    if (line.superview != self) {
      [self addSubview:line positioned:NSWindowAbove relativeTo:nil];
    }
    x += kGroupGap;
  }
}

#pragma mark - Tab interaction

- (void)tabViewClose:(BRTabView*)tabView {
  [_delegate tabStripDidCloseTab:tabView.tabId];
}

- (void)tabViewToggleMute:(BRTabView*)tabView {
  [_delegate tabStripDidToggleMute:tabView.tabId];
}

- (void)groupLabelClicked:(BRGroupLabelView*)label {
  if (label.autoCollapsed) {
    [_delegate tabStripDidActivateGroup:label.profileId];
  } else {
    [_delegate tabStripDidToggleGroup:label.profileId];
  }
}

- (void)plusClicked:(NSButton*)sender {
  [_delegate tabStripDidRequestNewTabInProfile:sender.identifier];
}

// Lists the group's hidden tabs; choosing one switches to it.
- (void)moreClicked:(NSButton*)sender {
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
  for (BRTabGroupInfo* group in _groups) {
    if (![group.profileId isEqualToString:sender.identifier]) {
      continue;
    }
    for (BRTab* tab in group.tabs) {
      BRTabView* view = [self viewOfTab:tab.tabId];
      if (view && !view.hidden) {
        continue;
      }
      NSString* title = tab.title.length ? tab.title : tab.url;
      NSMenuItem* item =
          [menu addItemWithTitle:title.length ? title : @"New Tab"
                          action:@selector(hiddenTabChosen:)
                   keyEquivalent:@""];
      item.target = self;
      item.tag = static_cast<NSInteger>(tab.tabId);
      NSImage* icon = [tab.favicon copy];
      icon.size = NSMakeSize(16, 16);
      item.image = icon;
    }
  }
  [menu popUpMenuPositioningItem:nil
                      atLocation:NSMakePoint(0, NSMaxY(sender.bounds) + 4)
                          inView:sender];
}

- (void)hiddenTabChosen:(NSMenuItem*)item {
  [_delegate tabStripDidSelectTab:static_cast<brouk::TabId>(item.tag)];
}

- (nullable BRTabView*)viewOfTab:(brouk::TabId)tabId {
  for (BRTabView* view in _tabViews) {
    if (view.tabId == tabId) {
      return view;
    }
  }
  return nil;
}

- (BOOL)isPinned:(brouk::TabId)tabId {
  for (BRTabGroupInfo* group in _groups) {
    for (NSUInteger i = 0; i < group.tabs.count; ++i) {
      if (group.tabs[i].tabId == tabId) {
        return i < group.pinnedCount;
      }
    }
  }
  return NO;
}

// The tab's right-click menu; each item carries the tab's id in its tag.
- (nullable NSMenu*)menuForTabView:(BRTabView*)tabView {
  BRTab* tab = _tabsById[@(tabView.tabId)];
  if (!tab) {
    return nil;
  }
  NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
  auto add = ^(NSString* title, SEL action) {
    NSMenuItem* item = [menu addItemWithTitle:title
                                       action:action
                                keyEquivalent:@""];
    item.target = self;
    item.tag = static_cast<NSInteger>(tabView.tabId);
  };
  add(@"Reload", @selector(menuReload:));
  add([self isPinned:tabView.tabId] ? @"Unpin Tab" : @"Pin Tab",
      @selector(menuTogglePin:));
  add(tab.isMuted ? @"Unmute Tab" : @"Mute Tab", @selector(menuToggleMute:));
  [menu addItem:NSMenuItem.separatorItem];
  add(@"Close Tab", @selector(menuClose:));
  return menu;
}

- (void)menuReload:(NSMenuItem*)item {
  [_delegate tabStripDidReloadTab:static_cast<brouk::TabId>(item.tag)];
}

- (void)menuTogglePin:(NSMenuItem*)item {
  auto tabId = static_cast<brouk::TabId>(item.tag);
  [_delegate tabStripDidSetPinned:![self isPinned:tabId] tab:tabId];
}

- (void)menuToggleMute:(NSMenuItem*)item {
  [_delegate tabStripDidToggleMute:static_cast<brouk::TabId>(item.tag)];
}

- (void)menuClose:(NSMenuItem*)item {
  [_delegate tabStripDidCloseTab:static_cast<brouk::TabId>(item.tag)];
}

- (nullable BRTabGroupInfo*)groupOfTab:(brouk::TabId)tabId {
  for (BRTabGroupInfo* group in _groups) {
    for (BRTab* tab in group.tabs) {
      if (tab.tabId == tabId) {
        return group;
      }
    }
  }
  return nil;
}

// The group whose area (label to "+") is under `x`, or the nearest one.
- (nullable BRTabGroupInfo*)groupAtX:(CGFloat)x {
  BRTabGroupInfo* nearest = nil;
  CGFloat best = CGFLOAT_MAX;
  for (BRTabGroupInfo* group in _groups) {
    CGFloat start = NSMinX(_labels[group.profileId].frame);
    CGFloat end = NSMaxX(_plusButtons[group.profileId].frame);
    if (x >= start && x <= end) {
      return group;
    }
    CGFloat distance = std::min(std::fabs(x - start), std::fabs(x - end));
    if (distance < best) {
      best = distance;
      nearest = group;
    }
  }
  return nearest;
}

// Click selects; a horizontal drag moves the tab (the dragged tab follows
// the mouse). Dropped in its own group, the slot under its centre is the new
// position; dropped on another profile's group, the delegate reopens it
// there.
- (void)tabViewMouseDown:(BRTabView*)tabView event:(NSEvent*)event {
  [_delegate tabStripDidSelectTab:tabView.tabId];

  NSPoint start = [self convertPoint:event.locationInWindow fromView:nil];
  NSRect startFrame = tabView.frame;
  BOOL dragging = NO;
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
    f.origin.x = std::clamp(startFrame.origin.x + dx, _leadingInset,
                            NSMaxX(self.bounds) - f.size.width);
    tabView.frame = f;
    [tabView.superview addSubview:tabView
                       positioned:NSWindowAbove
                       relativeTo:nil];
  }
  if (!dragging) {
    return;
  }
  CGFloat center = NSMidX(tabView.frame);
  BRTabGroupInfo* source = [self groupOfTab:tabView.tabId];
  BRTabGroupInfo* target = [self groupAtX:center];
  self.needsLayout = YES;
  if (!source || !target) {
    return;
  }
  if (![target.profileId isEqualToString:source.profileId]) {
    [_delegate tabStripDidDropTab:tabView.tabId onProfile:target.profileId];
    return;
  }
  // Position among the group's other tabs: after the last shown tab left
  // of the drop point (hidden tabs keep their place).
  NSUInteger position = 0;
  NSUInteger other = 0;
  for (BRTab* tab in source.tabs) {
    if (tab.tabId == tabView.tabId) {
      continue;
    }
    ++other;
    BRTabView* view = [self viewOfTab:tab.tabId];
    if (view && !view.hidden && NSMidX(view.frame) < center) {
      position = other;
    }
  }
  [_delegate tabStripDidMoveTab:tabView.tabId
                        toIndex:source.startIndex + position];
}

@end
