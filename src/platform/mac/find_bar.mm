#import "platform/mac/find_bar.h"

#import "platform/mac/menu_shortcuts.h"

namespace {

NSButton* IconButton(NSString* symbol,
                     NSString* label,
                     NSString* tooltip,
                     id target,
                     SEL action) {
  NSButton* button =
      [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:symbol
                                          accessibilityDescription:label]
                         target:target
                         action:action];
  button.bordered = NO;
  button.toolTip = tooltip;
  return button;
}

}  // namespace

@interface BRFindBar () <NSSearchFieldDelegate>
@end

@implementation BRFindBar {
  NSSearchField* _field;
  NSTextField* _count;
  NSButton* _previous;
  NSButton* _next;
  NSButton* _close;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.wantsLayer = YES;
    self.layer.cornerRadius = 8;
    self.layer.borderWidth = 1;
    self.layer.shadowOpacity = 0.2;
    self.layer.shadowRadius = 6;
    self.accessibilityRole = NSAccessibilityGroupRole;
    self.accessibilityLabel = @"Find in page";

    _field = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    _field.placeholderString = @"Find in page";
    _field.delegate = self;
    _field.sendsSearchStringImmediately = YES;
    _field.target = self;
    _field.action = @selector(fieldChanged:);
    _field.accessibilityLabel = @"Find in page";
    [self addSubview:_field];

    _count = [NSTextField labelWithString:@""];
    _count.textColor = NSColor.secondaryLabelColor;
    _count.font = [NSFont monospacedDigitSystemFontOfSize:12
                                                   weight:NSFontWeightRegular];
    _count.alignment = NSTextAlignmentRight;
    [self addSubview:_count];

    _previous = IconButton(@"chevron.up", @"Previous match", @"", self,
                           @selector(previousClicked:));
    _next = IconButton(@"chevron.down", @"Next match", @"", self,
                       @selector(nextClicked:));
    [self updateToolTips];
    [NSNotificationCenter.defaultCenter
        addObserver:self
           selector:@selector(updateToolTips)
               name:brouk::mac::kShortcutsChangedNotification
             object:nil];
    _close = IconButton(@"xmark", @"Close find bar", @"Close (Esc)", self,
                        @selector(closeClicked:));
    for (NSButton* b in @[ _previous, _next, _close ]) {
      [self addSubview:b];
    }
  }
  return self;
}

- (void)updateLayer {
  self.layer.backgroundColor = NSColor.windowBackgroundColor.CGColor;
  self.layer.borderColor = NSColor.separatorColor.CGColor;
}

- (BOOL)wantsUpdateLayer {
  return YES;
}

- (void)layout {
  [super layout];
  NSRect b = self.bounds;
  CGFloat x = NSMaxX(b) - 8;
  for (NSButton* button in @[ _close, _next, _previous ]) {
    x -= 26;
    button.frame = NSMakeRect(x, (b.size.height - 24) / 2, 24, 24);
  }
  x -= 64;
  _count.frame = NSMakeRect(x, (b.size.height - 16) / 2, 58, 16);
  _field.frame = NSMakeRect(8, (b.size.height - 24) / 2, x - 8 - 4, 24);
}

- (NSString*)text {
  return _field.stringValue;
}

- (void)setText:(NSString*)text {
  _field.stringValue = text;
}

- (void)focus {
  [self.window makeFirstResponder:_field];
  [_field.currentEditor selectAll:nil];
}

- (void)setActiveMatch:(int)active ofCount:(int)count {
  _count.stringValue =
      _field.stringValue.length == 0
          ? @""
          : [NSString stringWithFormat:@"%d/%d", count > 0 ? active : 0, count];
  _count.accessibilityValue =
      [NSString stringWithFormat:@"Match %d of %d", active, count];
}

- (void)fieldChanged:(id)sender {
  [_delegate findBar:self findText:_field.stringValue forward:YES findNext:NO];
}

- (void)updateToolTips {
  _previous.toolTip = brouk::mac::ToolTip(@"Previous", "find_previous");
  _next.toolTip = brouk::mac::ToolTip(@"Next", "find_next");
}

- (void)previousClicked:(id)sender {
  [_delegate findBar:self findText:_field.stringValue forward:NO findNext:YES];
}

- (void)nextClicked:(id)sender {
  [_delegate findBar:self findText:_field.stringValue forward:YES findNext:YES];
}

- (void)closeClicked:(id)sender {
  [_delegate findBarDidClose:self];
}

// Return: next match; Shift+Return: previous; Escape: close (as in Chrome).
- (BOOL)control:(NSControl*)control
               textView:(NSTextView*)textView
    doCommandBySelector:(SEL)selector {
  if (selector == @selector(insertNewline:)) {
    BOOL shift =
        (NSApp.currentEvent.modifierFlags & NSEventModifierFlagShift) != 0;
    [_delegate findBar:self
              findText:_field.stringValue
               forward:!shift
              findNext:YES];
    return YES;
  }
  if (selector == @selector(cancelOperation:)) {
    [_delegate findBarDidClose:self];
    return YES;
  }
  return NO;
}

@end
