#import "platform/mac/address_field.h"

#include <string>

#include "core/url_display.h"

@interface BRAddressField () <NSTextFieldDelegate>
@end

@implementation BRAddressField {
  BOOL _isEditing;
  BOOL _hasPendingText;
  NSString* _typedText;
  // The last edit added text at the end: inline completion is allowed.
  BOOL _allowInline;
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
// still selects part of the text, and later clicks place the cursor.
- (void)mouseDown:(NSEvent*)event {
  BOOL wasEditing = _isEditing;
  [super mouseDown:event];  // Returns when the mouse button is released.
  NSText* editor = self.currentEditor;
  if (!wasEditing && editor && editor.selectedRange.length == 0) {
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
