#import "platform/mac/address_suggestions_popup.h"

#include <algorithm>
#include <span>

namespace {

constexpr CGFloat kRowHeight = 30;
constexpr CGFloat kPadding = 4;

NSString* SymbolFor(brouk::AddressSuggestion::Kind kind) {
  switch (kind) {
    case brouk::AddressSuggestion::Kind::kTypedUrl:
      return @"globe";
    case brouk::AddressSuggestion::Kind::kHistory:
      return @"clock";
    case brouk::AddressSuggestion::Kind::kTypedSearch:
    case brouk::AddressSuggestion::Kind::kSearch:
      return @"magnifyingglass";
  }
  return @"globe";
}

NSString* KindLabel(brouk::AddressSuggestion::Kind kind) {
  switch (kind) {
    case brouk::AddressSuggestion::Kind::kTypedUrl:
      return @"Address";
    case brouk::AddressSuggestion::Kind::kHistory:
      return @"History";
    case brouk::AddressSuggestion::Kind::kTypedSearch:
    case brouk::AddressSuggestion::Kind::kSearch:
      return @"Search";
  }
  return @"";
}

}  // namespace

@class BRSuggestionRowView;

@interface BRSuggestionsPopup ()
- (void)rowViewClicked:(BRSuggestionRowView*)view;
- (void)rowViewHovered:(BRSuggestionRowView*)view;
@end

// One row: icon, text and, after it, the detail (address or "Search …").
@interface BRSuggestionRowView : NSView
@property(nonatomic) NSInteger row;
@property(nonatomic) BOOL selected;
@property(nonatomic, weak) BRSuggestionsPopup* popup;
- (void)showSuggestion:(const brouk::AddressSuggestion&)suggestion;
@end

@implementation BRSuggestionRowView {
  NSImageView* _icon;
  NSTextField* _text;
  NSTextField* _detail;
  NSTrackingArea* _tracking;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _icon = [NSImageView imageViewWithImage:[NSImage new]];
    _icon.contentTintColor = NSColor.secondaryLabelColor;
    _text = [NSTextField labelWithString:@""];
    _text.font = [NSFont systemFontOfSize:13];
    _text.lineBreakMode = NSLineBreakByTruncatingTail;
    _detail = [NSTextField labelWithString:@""];
    _detail.font = [NSFont systemFontOfSize:12];
    _detail.textColor = NSColor.secondaryLabelColor;
    _detail.lineBreakMode = NSLineBreakByTruncatingMiddle;
    for (NSView* view in @[ _icon, _text, _detail ]) {
      [self addSubview:view];
    }
    self.accessibilityRole = NSAccessibilityRowRole;
  }
  return self;
}

- (void)showSuggestion:(const brouk::AddressSuggestion&)suggestion {
  _icon.image = [NSImage imageWithSystemSymbolName:SymbolFor(suggestion.kind)
                          accessibilityDescription:nil];
  _text.stringValue = @(suggestion.text.c_str()) ?: @"";
  _detail.stringValue = @(suggestion.detail.c_str()) ?: @"";
  self.toolTip = @(suggestion.url.c_str());
  self.accessibilityLabel =
      [NSString stringWithFormat:@"%@: %@ %@", KindLabel(suggestion.kind),
                                 _text.stringValue, _detail.stringValue];
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
  _icon.frame = NSMakeRect(10, (b.size.height - 16) / 2, 16, 16);
  CGFloat x = 36;
  CGFloat available = b.size.width - x - 10;
  CGFloat textWidth =
      std::min(_text.intrinsicContentSize.width, available * 0.6);
  if (_detail.stringValue.length == 0) {
    textWidth = available;
  }
  _text.frame = NSMakeRect(x, (b.size.height - 17) / 2, textWidth, 17);
  _detail.frame =
      NSMakeRect(x + textWidth + 10, (b.size.height - 16) / 2,
                 std::max<CGFloat>(available - textWidth - 10, 0), 16);
}

- (void)drawRect:(NSRect)dirtyRect {
  if (_selected) {
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
  [_popup rowViewHovered:self];
}

- (BOOL)acceptsFirstMouse:(nullable NSEvent*)event {
  return YES;
}

- (void)mouseDown:(NSEvent*)event {
  [_popup rowViewClicked:self];
}

@end

@implementation BRSuggestionsPopup {
  NSPanel* _panel;
  NSMutableArray<BRSuggestionRowView*>* _rows;
  __weak NSWindow* _parent;
}

- (instancetype)init {
  if ((self = [super init])) {
    _rows = [NSMutableArray array];
    _selectedRow = -1;
  }
  return self;
}

- (BOOL)isVisible {
  return _panel.isVisible;
}

- (NSInteger)rowCount {
  return static_cast<NSInteger>(_rows.count);
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
  _panel.acceptsMouseMovedEvents = YES;
  NSVisualEffectView* background = [[NSVisualEffectView alloc] init];
  background.material = NSVisualEffectMaterialMenu;
  background.state = NSVisualEffectStateActive;
  background.wantsLayer = YES;
  background.layer.cornerRadius = 8;
  background.layer.masksToBounds = YES;
  background.accessibilityRole = NSAccessibilityListRole;
  background.accessibilityLabel = @"Suggestions";
  _panel.contentView = background;
}

- (void)showRows:(std::span<const brouk::AddressSuggestion>)rows
       belowView:(NSView*)field {
  if (rows.empty() || !field.window) {
    [self close];
    return;
  }
  if (!_panel) {
    [self buildPanel];
  }
  NSView* content = _panel.contentView;
  while (_rows.count > rows.size()) {
    [_rows.lastObject removeFromSuperview];
    [_rows removeLastObject];
  }
  while (_rows.count < rows.size()) {
    BRSuggestionRowView* view = [[BRSuggestionRowView alloc] init];
    view.popup = self;
    [content addSubview:view];
    [_rows addObject:view];
  }
  NSRect inWindow = [field convertRect:field.bounds toView:nil];
  NSRect onScreen = [field.window convertRectToScreen:inWindow];
  CGFloat height = rows.size() * kRowHeight + 2 * kPadding;
  NSRect frame = NSMakeRect(NSMinX(onScreen), NSMinY(onScreen) - 4 - height,
                            onScreen.size.width, height);
  [_panel setFrame:frame display:NO];
  for (std::size_t i = 0; i < rows.size(); ++i) {
    BRSuggestionRowView* view = _rows[i];
    view.row = static_cast<NSInteger>(i);
    view.frame = NSMakeRect(
        0, height - kPadding - (static_cast<CGFloat>(i) + 1) * kRowHeight,
        frame.size.width, kRowHeight);
    [view showSuggestion:rows[i]];
  }
  self.selectedRow = 0;
  if (!_panel.isVisible) {
    _parent = field.window;
    [field.window addChildWindow:_panel ordered:NSWindowAbove];
    [_panel orderFront:nil];
  }
}

- (void)setSelectedRow:(NSInteger)row {
  _selectedRow = row < static_cast<NSInteger>(_rows.count) ? row : -1;
  for (BRSuggestionRowView* view in _rows) {
    view.selected = view.row == _selectedRow;
  }
}

- (void)close {
  if (!_panel.isVisible) {
    return;
  }
  [_parent removeChildWindow:_panel];
  [_panel orderOut:nil];
  _selectedRow = -1;
}

- (void)rowViewClicked:(BRSuggestionRowView*)view {
  [_delegate suggestionsPopup:self didChooseRow:view.row];
}

- (void)rowViewHovered:(BRSuggestionRowView*)view {
  self.selectedRow = view.row;
}

@end
