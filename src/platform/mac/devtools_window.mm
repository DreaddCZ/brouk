#import "platform/mac/devtools_window.h"

#include "include/wrapper/cef_helpers.h"

@interface BRDevToolsWindowController () <BRDevToolsDockDelegate,
                                          NSWindowDelegate>
@end

@implementation BRDevToolsWindowController {
  __weak id<BRDevToolsWindowOwner> _owner;
  BRDevToolsDock* _dock;
}

- (instancetype)initWithInspectedBrowser:(CefRefPtr<CefBrowser>)inspected
                                   title:(NSString*)title
                                 inspect:(NSPoint)point
                                 context:(CefRefPtr<CefRequestContext>)context
                             preferences:
                                 (brouk::DevToolsPreferences*)preferences
                                 counter:(id<BRBrowserCounter>)counter
                                   owner:(id<BRDevToolsWindowOwner>)owner {
  CEF_REQUIRE_UI_THREAD();
  NSWindow* window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, 900, 600)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable |
                          NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  window.title = [NSString stringWithFormat:@"Developer Tools – %@", title];
  window.releasedWhenClosed = NO;
  window.restorable = NO;
  window.tabbingMode = NSWindowTabbingModeDisallowed;
  window.minSize = NSMakeSize(400, 300);
  [window center];
  if ((self = [super initWithWindow:window])) {
    _owner = owner;
    window.delegate = self;
    _dock = [[BRDevToolsDock alloc] initWithInspectedBrowser:inspected
                                                     inspect:point
                                                     context:context
                                                 preferences:preferences
                                                    ownTools:NO
                                                     counter:counter
                                                    delegate:self];
    _dock.view.frame = window.contentView.bounds;
    _dock.view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [window.contentView addSubview:_dock.view];
  }
  return self;
}

- (void)inspectAt:(NSPoint)point {
  [_dock inspectAt:point];
  [self showWindow:nil];
}

- (void)showPanel:(NSString*)panel {
  [_dock showPanel:panel];
  [self showWindow:nil];
}

- (void)enterInspectElementMode {
  [_dock enterInspectElementMode];
}

- (void)closeDevTools {
  [_dock close];  // devToolsDockDidClose: finishes.
}

- (void)detach {
  _owner = nil;
  [self finishClosing];
}

// The dock goes on closing its browsers by itself; the window goes now.
- (void)finishClosing {
  [_dock.view removeFromSuperview];
  [_dock detach];
  _dock = nil;
  self.window.delegate = nil;
  [self.window close];
}

// Cmd+Opt+I in this window closes it, as in Chrome's undocked DevTools.
- (IBAction)toggleDevTools:(id)sender {
  [self closeDevTools];
}

#pragma mark - BRDevToolsDockDelegate

- (void)devToolsDockDidClose:(BRDevToolsDock*)dock {
  if (dock != _dock) {
    return;
  }
  id<BRDevToolsWindowOwner> owner = _owner;
  _owner = nil;
  [self finishClosing];
  [owner devToolsWindowDidClose:self];
}

- (void)devToolsDock:(BRDevToolsDock*)dock wantsPosition:(NSString*)position {
  // Always a window: internal pages have no place for a dock.
}

- (BOOL)devToolsDockHandleKeyEvent:(NSEvent*)event {
  return [NSApp.mainMenu performKeyEquivalent:event];
}

- (void)devToolsDock:(BRDevToolsDock*)dock openURL:(NSString*)url {
  // Links in DevTools (documentation, sources) are web pages: they open in
  // a browser tab, never in the internal page.
  [NSApp sendAction:@selector(openURLFromDevTools:) to:nil from:url];
}

- (brouk::ToolsHost::Rules)devToolsDockRequestRules:(BRDevToolsDock*)dock {
  return {};
}

- (std::optional<brouk::Error>)devToolsDock:(BRDevToolsDock*)dock
                            setRequestRules:(brouk::RequestRuleSet)rules {
  return brouk::Error{"Internal pages have no request rules."};
}

- (void)devToolsDockRulesAffectedPage:(BRDevToolsDock*)dock {
}

#pragma mark - NSWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
  [self closeDevTools];
  return NO;  // Closed once DevTools has closed.
}

@end
