#import "platform/mac/export_password_dialog.h"

#include <utility>

namespace brouk::mac {

namespace {

constexpr NSUInteger kMinLength = 8;

}  // namespace

void AskExportPassword(NSWindow* window,
                       bool confirm,
                       NSString* message,
                       ExportPasswordDone done) {
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = confirm ? @"Protect the passwords with an export password"
                              : @"This profile contains passwords";
  alert.informativeText = message;
  [alert addButtonWithTitle:confirm ? @"Export" : @"Import"];
  [alert addButtonWithTitle:confirm ? @"Export Without Passwords"
                                    : @"Import Without Passwords"];
  [alert addButtonWithTitle:@"Cancel"];
  NSView* fields =
      [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 260, confirm ? 56 : 24)];
  NSSecureTextField* first = [[NSSecureTextField alloc]
      initWithFrame:NSMakeRect(0, confirm ? 32 : 0, 260, 24)];
  first.placeholderString = confirm ? @"Export password" : @"Export password";
  first.accessibilityLabel = @"Export password";
  [fields addSubview:first];
  NSSecureTextField* second = nil;
  if (confirm) {
    second =
        [[NSSecureTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
    second.placeholderString = @"Repeat the password";
    second.accessibilityLabel = @"Repeat the export password";
    [fields addSubview:second];
  }
  alert.accessoryView = fields;
  alert.window.initialFirstResponder = first;
  __block ExportPasswordDone answer = std::move(done);
  void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
    ExportPasswordAnswer result;
    if (response == NSAlertSecondButtonReturn) {
      result.choice = ExportPasswordAnswer::Choice::kWithoutPasswords;
    } else if (response == NSAlertFirstButtonReturn) {
      NSString* password = first.stringValue;
      if (confirm && ![password isEqualToString:second.stringValue]) {
        AskExportPassword(
            window, confirm,
            @"The two passwords are not the same. Type them again.",
            std::move(answer));
        return;
      }
      if (confirm && password.length < kMinLength) {
        AskExportPassword(window, confirm,
                          @"Use at least 8 characters, so the file cannot be "
                          @"opened by guessing.",
                          std::move(answer));
        return;
      }
      result.choice = ExportPasswordAnswer::Choice::kPassword;
      result.password = password.UTF8String ?: "";
    }
    first.stringValue = @"";
    second.stringValue = @"";
    answer(std::move(result));
  };
  if (window && window.visible) {
    [alert beginSheetModalForWindow:window completionHandler:completion];
  } else {
    completion([alert runModal]);
  }
}

}  // namespace brouk::mac
