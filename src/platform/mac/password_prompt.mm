#import "platform/mac/password_prompt.h"

#include <string>
#include <utility>

namespace {

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

std::string ToString(NSString* s) {
  const char* utf8 = s.UTF8String;
  return utf8 ? std::string(utf8) : std::string();
}

}  // namespace

@interface BRPasswordPrompt () <NSPopoverDelegate>
@end

@implementation BRPasswordPrompt {
  NSPopover* _popover;
  NSTextField* _username;
  NSSecureTextField* _secret;
  NSTextField* _plain;  // The password, shown.
  NSButton* _reveal;
  NSPopUpButton* _location;  // Bitwarden: where a new login goes.
  brouk::PasswordPromptDone _done;
  BRPasswordPrompt* _keepAlive;  // Until the popover closes.
}

+ (void)showPrompt:(const brouk::PasswordPrompt&)prompt
        relativeTo:(NSView*)anchor
              done:(brouk::PasswordPromptDone)done {
  BRPasswordPrompt* controller = [[BRPasswordPrompt alloc] init];
  [controller show:prompt relativeTo:anchor done:std::move(done)];
}

- (NSTextField*)label:(NSString*)text bold:(BOOL)bold {
  NSTextField* label = [NSTextField labelWithString:text];
  label.font =
      bold ? [NSFont boldSystemFontOfSize:13] : [NSFont systemFontOfSize:12];
  if (!bold) {
    label.textColor = NSColor.secondaryLabelColor;
  }
  return label;
}

- (void)show:(const brouk::PasswordPrompt&)prompt
    relativeTo:(NSView*)anchor
          done:(brouk::PasswordPromptDone)done {
  _done = std::move(done);
  _keepAlive = self;
  // Room for "Save in" (Bitwarden folders and collections).
  CGFloat extra = prompt.locations.empty() ? 0 : 34;
  NSView* content =
      [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 340, 210 + extra)];

  NSTextField* title =
      [self label:prompt.update ? @"Update password?" : @"Save password?"
             bold:YES];
  title.frame = NSMakeRect(16, 176 + extra, 308, 20);
  NSTextField* host = [self label:ToNSString(prompt.host) bold:NO];
  host.frame = NSMakeRect(16, 156 + extra, 308, 16);

  NSTextField* userLabel = [self label:@"User name" bold:NO];
  userLabel.frame = NSMakeRect(16, 126 + extra, 80, 16);
  _username = [NSTextField textFieldWithString:ToNSString(prompt.username)];
  _username.frame = NSMakeRect(100, 122 + extra, 224, 24);
  _username.accessibilityLabel = @"User name";

  NSTextField* passwordLabel = [self label:@"Password" bold:NO];
  passwordLabel.frame = NSMakeRect(16, 92 + extra, 80, 16);
  NSString* password = ToNSString(prompt.password);
  _secret = [[NSSecureTextField alloc]
      initWithFrame:NSMakeRect(100, 88 + extra, 194, 24)];
  _secret.stringValue = password;
  _secret.accessibilityLabel = @"Password";
  _plain = [NSTextField textFieldWithString:password];
  _plain.frame = _secret.frame;
  _plain.hidden = YES;
  _plain.accessibilityLabel = @"Password";
  _reveal = [NSButton
      buttonWithImage:[NSImage imageWithSystemSymbolName:@"eye"
                                accessibilityDescription:@"Show password"]
               target:self
               action:@selector(toggleReveal:)];
  _reveal.bordered = NO;
  _reveal.frame = NSMakeRect(298, 88 + extra, 26, 24);
  _reveal.toolTip = @"Show password";

  NSButton* save = [NSButton buttonWithTitle:prompt.update ? @"Update" : @"Save"
                                      target:self
                                      action:@selector(save:)];
  save.keyEquivalent = @"\r";
  save.frame = NSMakeRect(244, 16, 80, 28);
  NSButton* notNow = [NSButton buttonWithTitle:@"Not now"
                                        target:self
                                        action:@selector(notNow:)];
  notNow.frame = NSMakeRect(160, 16, 80, 28);
  notNow.keyEquivalent = @"\033";
  [content addSubview:title];
  [content addSubview:host];
  [content addSubview:userLabel];
  [content addSubview:_username];
  [content addSubview:passwordLabel];
  [content addSubview:_secret];
  [content addSubview:_plain];
  [content addSubview:_reveal];
  [content addSubview:save];
  [content addSubview:notNow];
  if (!prompt.locations.empty()) {
    NSTextField* locationLabel = [self label:@"Save in" bold:NO];
    locationLabel.frame = NSMakeRect(16, 92, 80, 16);
    _location = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(97, 86, 230, 26)
                                           pullsDown:NO];
    for (const std::string& location : prompt.locations) {
      [_location addItemWithTitle:ToNSString(location)];
    }
    _location.accessibilityLabel = @"Save in";
    [content addSubview:locationLabel];
    [content addSubview:_location];
  }
  if (!prompt.update) {
    NSButton* never = [NSButton buttonWithTitle:@"Never for this site"
                                         target:self
                                         action:@selector(never:)];
    never.bezelStyle = NSBezelStyleInline;
    never.bordered = NO;
    never.contentTintColor = NSColor.linkColor;
    never.frame = NSMakeRect(10, 20, 140, 22);
    [content addSubview:never];
  }

  NSViewController* controller = [[NSViewController alloc] init];
  controller.view = content;
  _popover = [[NSPopover alloc] init];
  _popover.contentViewController = controller;
  _popover.behavior = NSPopoverBehaviorSemitransient;
  _popover.delegate = self;
  [_popover showRelativeToRect:anchor.bounds
                        ofView:anchor
                 preferredEdge:NSRectEdgeMaxY];
  [content.window makeFirstResponder:save];
}

- (void)toggleReveal:(id)sender {
  BOOL show = _plain.hidden;
  if (show) {
    _plain.stringValue = _secret.stringValue;
  } else {
    _secret.stringValue = _plain.stringValue;
  }
  _plain.hidden = !show;
  _secret.hidden = show;
  _reveal.image = [NSImage
      imageWithSystemSymbolName:show ? @"eye.slash" : @"eye"
       accessibilityDescription:show ? @"Hide password" : @"Show password"];
  _reveal.toolTip = show ? @"Hide password" : @"Show password";
}

- (void)answer:(brouk::PasswordPromptAnswer::Choice)choice {
  brouk::PasswordPromptDone done = std::move(_done);
  _done = nullptr;
  if (done) {
    brouk::PasswordPromptAnswer answer;
    answer.choice = choice;
    answer.username = ToString(_username.stringValue);
    answer.password =
        ToString(_plain.hidden ? _secret.stringValue : _plain.stringValue);
    answer.location =
        _location ? static_cast<int>(_location.indexOfSelectedItem) : 0;
    done(std::move(answer));
  }
  _secret.stringValue = @"";
  _plain.stringValue = @"";
  [_popover close];
}

- (void)save:(id)sender {
  [self answer:brouk::PasswordPromptAnswer::Choice::kSave];
}

- (void)never:(id)sender {
  [self answer:brouk::PasswordPromptAnswer::Choice::kNever];
}

- (void)notNow:(id)sender {
  [self answer:brouk::PasswordPromptAnswer::Choice::kNotNow];
}

- (void)popoverDidClose:(NSNotification*)notification {
  if (_done) {
    [self answer:brouk::PasswordPromptAnswer::Choice::kNotNow];
  }
  _keepAlive = nil;
}

@end
