// The standard round "?" button of macOS, opening help at a section (016).
// The anchors are checked at build time (web/help/build.mjs). Main thread.

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

// Sends -openHelpForButton: up the responder chain to the app, which reads
// the anchor from the button's identifier.
NSButton* BRHelpButton(NSString* anchor);

NS_ASSUME_NONNULL_END
