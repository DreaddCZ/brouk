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
  BRTabDialogCompletion _completion;
  BOOL _hasCancel;
  NSView* _panel;
  NSTextField* _input;
  NSButton* _suppress;
  NSButton* _okButton;
}

+ (instancetype)dialogForRequest:(const brouk::JsDialogRequest&)request
                            done:(brouk::JsDialogDone)done {
  using Kind = brouk::JsDialogRequest::Kind;
  NSString* title;
  NSString* message;
  NSString* okTitle = @"OK";
  NSString* cancelTitle = request.kind == Kind::kAlert ? nil : @"Cancel";
  if (request.kind == Kind::kBeforeUnload) {
    // Like Chrome, the page's own text is not shown (it could mislead).
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
  NSString* prompt = request.kind == Kind::kPrompt
                         ? ToNSString(request.default_prompt_text)
                         : nil;
  __block brouk::JsDialogDone js_done = std::move(done);
  return [[BRTabDialogView alloc]
         initWithTitle:title
               message:message
               okTitle:okTitle
           cancelTitle:cancelTitle
            promptText:prompt
      offerSuppression:request.offer_suppression
            completion:^(BOOL ok, NSString* input, BOOL suppress) {
              js_done(ok, input.UTF8String ?: "", suppress);
            }];
}

- (instancetype)initWithTitle:(NSString*)title
                      message:(NSString*)message
                      okTitle:(NSString*)okTitle
                  cancelTitle:(nullable NSString*)cancelTitle
                   promptText:(nullable NSString*)promptText
             offerSuppression:(BOOL)offerSuppression
                   completion:(BRTabDialogCompletion)completion {
  if ((self = [super initWithFrame:NSZeroRect])) {
    _completion = completion;
    _hasCancel = cancelTitle != nil;
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    self.wantsLayer = YES;
    self.layer.backgroundColor =
        [NSColor.blackColor colorWithAlphaComponent:0.25].CGColor;
    [self buildPanelWithTitle:title
                      message:message
                      okTitle:okTitle
                  cancelTitle:cancelTitle
                   promptText:promptText
             offerSuppression:offerSuppression];
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

- (void)buildPanelWithTitle:(NSString*)title
                    message:(NSString*)message
                    okTitle:(NSString*)okTitle
                cancelTitle:(nullable NSString*)cancelTitle
                 promptText:(nullable NSString*)promptText
           offerSuppression:(BOOL)offerSuppression {
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

  // The message: selectable, scrolls if very long.
  NSTextView* text = [[NSTextView alloc] initWithFrame:NSZeroRect];
  text.string = message;
  text.editable = NO;
  text.selectable = YES;
  text.drawsBackground = NO;
  text.font = [NSFont systemFontOfSize:13];
  text.textColor = NSColor.labelColor;
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

  if (promptText) {
    _input = [NSTextField textFieldWithString:promptText];
    _input.accessibilityLabel = @"Your answer";
    _input.target = self;
    _input.action = @selector(okClicked:);
  }
  if (offerSuppression) {
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
  if (cancelTitle) {
    cancelButton = [NSButton buttonWithTitle:cancelTitle
                                      target:self
                                      action:@selector(cancelClicked:)];
    cancelButton.keyEquivalent = @"\x1b";  // Escape.
  }

  // Layout, bottom-up (the panel is not flipped).
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
  BRTabDialogCompletion completion = _completion;
  _completion = nil;
  NSString* input = _input.stringValue ?: @"";
  BOOL suppress = _suppress.state == NSControlStateValueOn;
  [self removeFromSuperview];
  if (completion) {
    completion(ok, input, suppress);
  }
}

- (void)okClicked:(id)sender {
  [self finish:YES];
}

- (void)cancelClicked:(id)sender {
  [self finish:NO];
}

// Escape without a Cancel button (alert) closes it like OK.
- (void)cancelOperation:(id)sender {
  [self finish:!_hasCancel];
}

- (void)dismiss {
  _completion = nil;
  [self removeFromSuperview];
}

@end
