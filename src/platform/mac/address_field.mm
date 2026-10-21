#import "platform/mac/address_field.h"

#include <string>

#include "core/url_display.h"

@interface BRAddressField () <NSTextFieldDelegate>
@end

@implementation BRAddressField {
  BOOL _isEditing;
  BOOL _hasPendingText;
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
  }
  return self;
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

- (void)focusAndSelectAll {
  if (_isEditing && self.currentEditor) {
    [self.currentEditor selectAll:nil];
  } else {
    [self.window makeFirstResponder:self];
  }
}

- (void)controlTextDidEndEditing:(NSNotification*)notification {
  _isEditing = NO;
  if (!_hasPendingText) {
    [self showDisplayText];
  }
}

- (void)controlTextDidChange:(NSNotification*)notification {
  _hasPendingText = YES;
  [_addressDelegate addressFieldDidEdit:self];
}

- (BOOL)control:(NSControl*)control
               textView:(NSTextView*)textView
    doCommandBySelector:(SEL)selector {
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
