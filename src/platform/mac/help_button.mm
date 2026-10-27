#import "platform/mac/help_button.h"

NSButton* BRHelpButton(NSString* anchor) {
  NSButton* button = [[NSButton alloc] init];
  button.bezelStyle = NSBezelStyleHelpButton;
  button.title = @"";
  button.identifier = anchor;
  button.action = NSSelectorFromString(@"openHelpForButton:");
  button.target = nil;
  button.toolTip = @"Help";
  button.accessibilityLabel = @"Help";
  return button;
}
