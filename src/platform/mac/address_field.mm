#import "platform/mac/address_field.h"

#include <string>

#include "core/url_display.h"

// Keeps the text, shown or edited, clear of the field's right end, where
// the window puts its indicators.
@interface BRAddressFieldCell : NSTextFieldCell
@property(nonatomic) CGFloat trailingInset;
@end

@implementation BRAddressFieldCell

- (NSRect)textRectForBounds:(NSRect)rect {
  rect.size.width = MAX(0, rect.size.width - _trailingInset);
  return rect;
}

- (NSRect)drawingRectForBounds:(NSRect)rect {
  return [super drawingRectForBounds:[self textRectForBounds:rect]];
}

- (void)editWithFrame:(NSRect)rect
               inView:(NSView*)controlView
               editor:(NSText*)textObj
             delegate:(nullable id)delegate
                event:(nullable NSEvent*)event {
  [super editWithFrame:[self textRectForBounds:rect]
                inView:controlView
                editor:textObj
              delegate:delegate
                 event:event];
}

- (void)selectWithFrame:(NSRect)rect
                 inView:(NSView*)controlView
                 editor:(NSText*)textObj
               delegate:(nullable id)delegate
                  start:(NSInteger)selStart
                 length:(NSInteger)selLength {
  [super selectWithFrame:[self textRectForBounds:rect]
                  inView:controlView
                  editor:textObj
                delegate:delegate
                   start:selStart
                  length:selLength];
}

@end

@interface BRAddressField () <NSTextFieldDelegate>
@end

@implementation BRAddressField {
  BOOL _isEditing;
  BOOL _hasPendingText;
  NSString* _typedText;
  // The last edit added text at the end: inline completion is allowed.
  BOOL _allowInline;
  // The field got the focus from the mouse click being handled.
  BOOL _focusedByClick;
}

+ (Class)cellClass {
  return [BRAddressFieldCell class];
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.delegate = self;
    self.bezelStyle = NSTextFieldRoundedBezel;
    self.font = [NSFont systemFontOfSize:13];
    self.placeholderString = @"Search or enter address";
    self.lineBreakMode = NSLineBreakByTruncatingTail;
    self.cell.usesSingleLineMode = YES;
    self.cell.scrollable = YES;
    self.accessibilityLabel = @"Address and search bar";
    _pageURL = @"";
    _typedText = @"";
  }
  return self;
}

- (CGFloat)trailingInset {
  return static_cast<BRAddressFieldCell*>(self.cell).trailingInset;
}

- (void)setTrailingInset:(CGFloat)inset {
  auto* cell = static_cast<BRAddressFieldCell*>(self.cell);
  if (cell.trailingInset == inset) {
    return;
  }
  cell.trailingInset = inset;
  // An open field editor keeps the frame it was given: move it.
  if (NSText* editor = self.currentEditor) {
    NSRect frame = [cell textRectForBounds:self.bounds];
    editor.superview.frame = [cell drawingRectForBounds:frame];
  }
  self.needsDisplay = YES;
}

- (NSString*)typedText {
  return _typedText;
}

// Replaces the field editor's text without a change notification.
- (void)replaceEditorText:(NSString*)text selection:(NSRange)selection {
  NSText* editor = self.currentEditor;
  if (!editor) {
    return;
  }
  editor.string = text;
  editor.selectedRange = selection;
}

- (void)setInlineCompletion:(NSString*)completion
               forTypedText:(NSString*)typed {
  if (!_allowInline || completion.length == 0 ||
      ![typed isEqualToString:_typedText] ||
      ![self.currentEditor.string isEqualToString:typed]) {
    return;
  }
  [self replaceEditorText:[typed stringByAppendingString:completion]
                selection:NSMakeRange(typed.length, completion.length)];
}

- (void)showSuggestionText:(nullable NSString*)text {
  NSString* shown = text ?: _typedText;
  [self replaceEditorText:shown selection:NSMakeRange(shown.length, 0)];
}

- (BOOL)isEditing {
  return _isEditing;
}

- (void)setPageURL:(NSString*)pageURL {
  _pageURL = [pageURL copy] ?: @"";
  if (!_isEditing && !_hasPendingText) {
    [self showDisplayText];
  }
}

- (void)showDisplayText {
  std::string display = brouk::FormatUrlForDisplay(_pageURL.UTF8String);
  self.stringValue = [NSString stringWithUTF8String:display.c_str()] ?: @"";
}

- (void)showPendingText:(nullable NSString*)text {
  _hasPendingText = text != nil;
  if (text) {
    self.stringValue = text;
  } else {
    [self showDisplayText];
  }
}

- (BOOL)becomeFirstResponder {
  BOOL ok = [super becomeFirstResponder];
  if (ok) {
    _isEditing = YES;
    _focusedByClick = NSApp.currentEvent.type == NSEventTypeLeftMouseDown;
    _typedText = @"";
    _allowInline = NO;
    if (!_hasPendingText) {
      // Edit the real URL, not the shortened form.
      std::string display = brouk::FormatUrlForDisplay(_pageURL.UTF8String);
      self.stringValue = display.empty() ? @"" : _pageURL;
    }
    // Select everything once the field editor is attached.
    dispatch_async(dispatch_get_main_queue(), ^{
      [self.currentEditor selectAll:nil];
    });
  }
  return ok;
}

// The first click into the field selects everything, as in Chrome; the
// click itself would otherwise put the cursor where it landed. Dragging
// still selects part of the text, and later clicks place the cursor. The
// window focuses the field before it passes on the click, so whether this
// click focused it is remembered in becomeFirstResponder.
- (void)mouseDown:(NSEvent*)event {
  BOOL wasEditing = _isEditing && !_focusedByClick;
  [super mouseDown:event];  // Returns when the mouse button is released.
  BOOL firstClick = !wasEditing || _focusedByClick;
  _focusedByClick = NO;
  NSText* editor = self.currentEditor;
  if (firstClick && editor && editor.selectedRange.length == 0) {
    [editor selectAll:nil];
  }
}

- (void)focusAndSelectAll {
  if (_isEditing && self.currentEditor) {
    [self.currentEditor selectAll:nil];
  } else {
    [self.window makeFirstResponder:self];
  }
}

- (void)controlTextDidEndEditing:(NSNotification*)notification {
  _isEditing = NO;
  _focusedByClick = NO;
  if ([_addressDelegate
          respondsToSelector:@selector(addressFieldDidEndEditing:)]) {
    [_addressDelegate addressFieldDidEndEditing:self];
  }
  if (!_hasPendingText) {
    [self showDisplayText];
  }
}

- (void)controlTextDidChange:(NSNotification*)notification {
  _hasPendingText = YES;
  NSTextView* editor = (NSTextView*)self.currentEditor;
  NSString* text = editor.string ?: self.stringValue;
  NSRange selection = editor.selectedRange;
  BOOL composing =
      [editor isKindOfClass:NSTextView.class] && editor.hasMarkedText;
  _allowInline = !composing && text.length > _typedText.length &&
                 [text hasPrefix:_typedText] &&
                 selection.location == text.length && selection.length == 0;
  _typedText = [text copy];
  [_addressDelegate addressFieldDidEdit:self];
}

- (BOOL)control:(NSControl*)control
               textView:(NSTextView*)textView
    doCommandBySelector:(SEL)selector {
  BOOL shift =
      (NSApp.currentEvent.modifierFlags & NSEventModifierFlagShift) != 0;
  BOOL forList = selector == @selector(moveUp:) ||
                 selector == @selector(moveDown:) ||
                 selector == @selector(cancelOperation:) ||
                 (shift && (selector == @selector(deleteBackward:) ||
                            selector == @selector(deleteForward:)));
  if (forList &&
      [_addressDelegate
          respondsToSelector:@selector(addressField:doCommand:)] &&
      [_addressDelegate addressField:self doCommand:selector]) {
    return YES;
  }
  if (selector == @selector(insertNewline:)) {
    NSString* text = self.stringValue;
    _hasPendingText = NO;
    [_addressDelegate addressField:self didSubmit:text];
    return YES;
  }
  if (selector == @selector(cancelOperation:)) {
    _hasPendingText = NO;
    std::string display = brouk::FormatUrlForDisplay(_pageURL.UTF8String);
    self.stringValue = display.empty() ? @"" : _pageURL;
    [self.currentEditor selectAll:nil];
    [_addressDelegate addressFieldDidCancel:self];
    return YES;
  }
  return NO;
}

@end
