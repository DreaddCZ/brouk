#import "platform/mac/tab_dialog_view.h"

#include <string>
#include <utility>

namespace {

constexpr CGFloat kPanelWidth = 440;
constexpr CGFloat kPadding = 20;
constexpr CGFloat kTopOffset = 16;
constexpr CGFloat kMaxMessageHeight = 300;

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

}  // namespace

@implementation BRTabDialogView {
  brouk::JsDialogDone _done;
  brouk::JsDialogRequest::Kind _kind;
  NSView* _panel;
  NSTextField* _input;
  NSButton* _suppress;
  NSButton* _okButton;
}

- (instancetype)initWithRequest:(const brouk::JsDialogRequest&)request
                           done:(brouk::JsDialogDone)done {
  if ((self = [super initWithFrame:NSZeroRect])) {
    _done = std::move(done);
    _kind = request.kind;
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.wantsLayer = YES;
    self.layer.backgroundColor =
        [NSColor.blackColor colorWithAlphaComponent:0.25].CGColor;
    [self buildPanel:request];
  }
  return self;
}

- (BOOL)isFlipped {
  return YES;
}

// Swallow clicks so the page underneath cannot be used meanwhile.
- (NSView*)hitTest:(NSPoint)point {
  NSView* hit = [super hitTest:point];
  return hit ?: self;
}

- (void)mouseDown:(NSEvent*)event {
}

- (void)buildPanel:(const brouk::JsDialogRequest&)request {
  using Kind = brouk::JsDialogRequest::Kind;
  BOOL beforeUnload = request.kind == Kind::kBeforeUnload;

  NSString* title;
  NSString* message;
  NSString* okTitle = @"OK";
  if (beforeUnload) {
    title = request.is_reload ? @"Reload site?" : @"Leave site?";
    message = @"Changes you made may not be saved.";
    okTitle = request.is_reload ? @"Reload" : @"Leave";
  } else {
    title = request.origin.empty()
                ? @"This page says"
                : [NSString
                      stringWithFormat:@"%@ says", ToNSString(request.origin)];
    message = ToNSString(request.message);
  }

  _panel = [[NSView alloc] initWithFrame:NSZeroRect];
  _panel.wantsLayer = YES;
  _panel.layer.cornerRadius = 10;
  _panel.layer.backgroundColor = NSColor.windowBackgroundColor.CGColor;
  _panel.layer.borderColor = NSColor.separatorColor.CGColor;
  _panel.layer.borderWidth = 1;
  _panel.layer.shadowOpacity = 0.3;
  _panel.layer.shadowRadius = 12;
  _panel.accessibilityRole = NSAccessibilitySheetRole;
  _panel.accessibilityLabel = title;
  [self addSubview:_panel];

  NSTextField* titleLabel = [NSTextField labelWithString:title];
  titleLabel.font = [NSFont boldSystemFontOfSize:13];
  titleLabel.lineBreakMode = NSLineBreakByTruncatingMiddle;

  // The page's text: selectable, scrolls if very long.
  NSTextView* text = [[NSTextView alloc] initWithFrame:NSZeroRect];
  text.string = message;
  text.editable = NO;
  text.selectable = YES;
  text.drawsBackground = NO;
  text.font = [NSFont systemFontOfSize:13];
  text.textContainerInset = NSZeroSize;
  text.textContainer.lineFragmentPadding = 0;
  CGFloat contentWidth = kPanelWidth - 2 * kPadding;
  text.frame = NSMakeRect(0, 0, contentWidth, 10);
  text.textContainer.containerSize = NSMakeSize(contentWidth, CGFLOAT_MAX);
  [text.layoutManager ensureLayoutForTextContainer:text.textContainer];
  CGFloat textHeight =
      std::ceil([text.layoutManager usedRectForTextContainer:text.textContainer]
                    .size.height);
  text.frame = NSMakeRect(0, 0, contentWidth, textHeight);
  NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  scroll.documentView = text;
  scroll.hasVerticalScroller = textHeight > kMaxMessageHeight;
  scroll.drawsBackground = NO;
  scroll.borderType = NSNoBorder;
  CGFloat messageHeight = std::min(textHeight, kMaxMessageHeight);

  if (request.kind == Kind::kPrompt) {
    _input = [NSTextField
        textFieldWithString:ToNSString(request.default_prompt_text)];
    _input.accessibilityLabel = @"Your answer";
    _input.target = self;
    _input.action = @selector(okClicked:);
  }
  if (request.offer_suppression) {
    _suppress = [NSButton
        checkboxWithTitle:@"Prevent this page from creating additional dialogs"
                   target:nil
                   action:nil];
  }

  _okButton = [NSButton buttonWithTitle:okTitle
                                 target:self
                                 action:@selector(okClicked:)];
  _okButton.keyEquivalent = @"\r";
  NSButton* cancelButton = nil;
  if (request.kind != Kind::kAlert) {
    cancelButton = [NSButton buttonWithTitle:@"Cancel"
                                      target:self
                                      action:@selector(cancelClicked:)];
    cancelButton.keyEquivalent = @"\x1b";  // Escape.
  }

  // Layout, top to bottom (the panel is not flipped: build bottom-up).
  CGFloat y = kPadding;
  CGFloat buttonHeight = _okButton.fittingSize.height;
  CGFloat okWidth = std::max<CGFloat>(_okButton.fittingSize.width, 80);
  _okButton.frame =
      NSMakeRect(kPanelWidth - kPadding - okWidth, y, okWidth, buttonHeight);
  [_panel addSubview:_okButton];
  if (cancelButton) {
    CGFloat w = std::max<CGFloat>(cancelButton.fittingSize.width, 80);
    cancelButton.frame =
        NSMakeRect(NSMinX(_okButton.frame) - 8 - w, y, w, buttonHeight);
    [_panel addSubview:cancelButton];
  }
  y += buttonHeight + 14;
  if (_suppress) {
    _suppress.frame =
        NSMakeRect(kPadding, y, contentWidth, _suppress.fittingSize.height);
    [_panel addSubview:_suppress];
    y += _suppress.frame.size.height + 10;
  }
  if (_input) {
    _input.frame =
        NSMakeRect(kPadding, y, contentWidth, _input.fittingSize.height);
    [_panel addSubview:_input];
    y += _input.frame.size.height + 10;
  }
  if (message.length > 0) {
    scroll.frame = NSMakeRect(kPadding, y, contentWidth, messageHeight);
    [_panel addSubview:scroll];
    y += messageHeight + 10;
  }
  CGFloat titleHeight = titleLabel.fittingSize.height;
  titleLabel.frame = NSMakeRect(kPadding, y, contentWidth, titleHeight);
  [_panel addSubview:titleLabel];
  y += titleHeight + kPadding;
  _panel.frame = NSMakeRect(0, 0, kPanelWidth, y);
}

- (void)layout {
  [super layout];
  NSRect b = self.bounds;
  NSRect f = _panel.frame;
  f.origin.x = std::floor((b.size.width - f.size.width) / 2);
  f.origin.y = kTopOffset;  // Flipped: near the top of the tab, as Chrome.
  _panel.frame = f;
}

- (void)focus {
  [self.window makeFirstResponder:_input ?: static_cast<NSView*>(_okButton)];
}

- (void)finish:(BOOL)ok {
  brouk::JsDialogDone done = std::move(_done);
  _done = nullptr;
  std::string input = _input ? std::string(_input.stringValue.UTF8String) : "";
  BOOL suppress = _suppress.state == NSControlStateValueOn;
  [self removeFromSuperview];
  if (done) {
    done(ok, input, suppress);
  }
}

- (void)okClicked:(id)sender {
  [self finish:YES];
}

- (void)cancelClicked:(id)sender {
  [self finish:NO];
}

// Escape on an alert closes it like OK.
- (void)cancelOperation:(id)sender {
  [self finish:_kind == brouk::JsDialogRequest::Kind::kAlert];
}

- (void)dismiss {
  _done = nullptr;
  [self removeFromSuperview];
}

@end
