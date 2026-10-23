#import "platform/mac/devtools_dock.h"

#include <memory>
#include <set>
#include <string>

#include "cef/devtools_frontend_client.h"
#include "core/devtools_frontend.h"
#include "include/wrapper/cef_helpers.h"

@interface BRDevToolsDock ()
- (void)handleCreated:(CefRefPtr<CefBrowser>)frontend;
- (void)handleCloseReady;
- (void)handleClosed;
- (BOOL)handleKeyEvent:(NSEvent*)event;
- (void)handleWantsClose;
- (void)handleOpenURL:(NSString*)url;
@end

namespace brouk {

class DevToolsDockBridge : public DevToolsFrontendDelegate {
 public:
  explicit DevToolsDockBridge(BRDevToolsDock* dock) : dock_(dock) {}
  void OnDevToolsCreated(CefRefPtr<CefBrowser> frontend) override {
    [dock_ handleCreated:frontend];
  }
  void OnDevToolsCloseReady() override { [dock_ handleCloseReady]; }
  void OnDevToolsClosed() override { [dock_ handleClosed]; }
  bool OnDevToolsKeyEvent(const CefKeyEvent& event,
                          CefEventHandle os_event) override {
    NSEvent* ns_event = (__bridge NSEvent*)os_event;
    return ns_event && event.type == KEYEVENT_RAWKEYDOWN &&
           ns_event.type == NSEventTypeKeyDown &&
           [dock_ handleKeyEvent:ns_event];
  }
  void OnDevToolsWantsClose() override { [dock_ handleWantsClose]; }
  void OpenUrlFromDevTools(const std::string& url) override {
    [dock_ handleOpenURL:@(url.c_str())];
  }
  void CopyTextFromDevTools(const std::string& text) override {
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    [pasteboard setString:@(text.c_str()) ?: @""
                  forType:NSPasteboardTypeString];
  }

 private:
  __weak BRDevToolsDock* dock_;
};

}  // namespace brouk

namespace {

constexpr CGFloat kHeaderHeight = 28;

// Docks whose tab is gone while their DevTools browser is still closing.
std::set<BRDevToolsDock*>& ClosingDocks() {
  static std::set<BRDevToolsDock*> docks;
  return docks;
}

NSButton* HeaderButton(NSString* symbol,
                       NSString* label,
                       id target,
                       SEL action) {
  NSButton* button =
      [NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:symbol
                                          accessibilityDescription:label]
                         target:target
                         action:action];
  button.bordered = NO;
  button.toolTip = label;
  button.symbolConfiguration = [NSImageSymbolConfiguration
      configurationWithPointSize:12
                          weight:NSFontWeightRegular];
  return button;
}

}  // namespace

// The dock's header: tool tabs on the left, position and close buttons on
// the right.
@interface BRDevToolsHeaderView : NSView
@end

@implementation BRDevToolsHeaderView
- (BOOL)isFlipped {
  return YES;
}
- (void)drawRect:(NSRect)dirtyRect {
  [[NSColor.windowBackgroundColor blendedColorWithFraction:0.06
                                                   ofColor:NSColor.labelColor]
      setFill];
  NSRectFill(self.bounds);
  [NSColor.separatorColor setFill];
  NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 1));
  NSRectFill(
      NSMakeRect(0, self.bounds.size.height - 1, self.bounds.size.width, 1));
}
@end

@implementation BRDevToolsDock {
  __weak id<BRDevToolsDockDelegate> _delegate;
  std::unique_ptr<brouk::DevToolsDockBridge> _bridge;
  CefRefPtr<brouk::DevToolsFrontendClient> _client;
  CefRefPtr<CefBrowser> _frontend;
  id<BRBrowserCounter> _counter;
  BOOL _counted;
  NSView* _content;
  NSDictionary<NSString*, NSButton*>* _positionButtons;
}

- (instancetype)initWithInspectedBrowser:(CefRefPtr<CefBrowser>)inspected
                                 inspect:(NSPoint)point
                                 context:(CefRefPtr<CefRequestContext>)context
                             preferences:
                                 (brouk::DevToolsPreferences*)preferences
                                 counter:(id<BRBrowserCounter>)counter
                                delegate:(id<BRDevToolsDockDelegate>)delegate {
  CEF_REQUIRE_UI_THREAD();
  if ((self = [super init])) {
    _delegate = delegate;
    _counter = counter;
    _position = @"bottom";
    [self buildViews];
    _bridge = std::make_unique<brouk::DevToolsDockBridge>(self);
    _client = new brouk::DevToolsFrontendClient(_bridge.get(), inspected,
                                                preferences);
    if (point.x != NSNotFound) {
      _client->InspectAt(static_cast<int>(point.x), static_cast<int>(point.y));
    }
    NSRect bounds = _content.bounds;
    CefWindowInfo info;
    info.SetAsChild((__bridge void*)_content,
                    CefRect(0, 0, static_cast<int>(bounds.size.width),
                            static_cast<int>(bounds.size.height)));
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    CefBrowserHost::CreateBrowser(info, _client,
                                  std::string(brouk::kDevToolsFrontendUrl),
                                  CefBrowserSettings(), nullptr, context);
  }
  return self;
}

- (void)buildViews {
  _view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 300)];
  _view.wantsLayer = YES;
  BRDevToolsHeaderView* header = [[BRDevToolsHeaderView alloc]
      initWithFrame:NSMakeRect(0, 0, 600, kHeaderHeight)];
  header.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  header.frame = NSMakeRect(0, 300 - kHeaderHeight, 600, kHeaderHeight);

  // Tool tabs (our own tools join here, 006).
  NSTextField* tool = [NSTextField labelWithString:@"DevTools"];
  tool.font = [NSFont systemFontOfSize:12 weight:NSFontWeightSemibold];
  tool.frame = NSMakeRect(10, 6, 120, 16);
  [header addSubview:tool];

  NSMutableDictionary* buttons = [NSMutableDictionary dictionary];
  NSArray* positions = @[
    @[ @"bottom", @"rectangle.bottomthird.inset.filled", @"Dock to bottom" ],
    @[ @"left", @"rectangle.leftthird.inset.filled", @"Dock to left" ],
    @[ @"right", @"rectangle.rightthird.inset.filled", @"Dock to right" ],
    @[ @"window", @"macwindow.on.rectangle", @"Open in a separate window" ],
  ];
  CGFloat x = 600 - 28;
  NSButton* close = HeaderButton(@"xmark", @"Close developer tools (⌥⌘I)", self,
                                 @selector(closeClicked:));
  close.frame = NSMakeRect(x, 3, 24, 22);
  close.autoresizingMask = NSViewMinXMargin;
  [header addSubview:close];
  x -= 10;
  for (NSArray* item in positions.reverseObjectEnumerator) {
    x -= 26;
    NSButton* button =
        HeaderButton(item[1], item[2], self, @selector(positionClicked:));
    button.identifier = item[0];
    button.frame = NSMakeRect(x, 3, 24, 22);
    button.autoresizingMask = NSViewMinXMargin;
    [header addSubview:button];
    buttons[item[0]] = button;
  }
  _positionButtons = buttons;
  [_view addSubview:header];

  _content =
      [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 300 - kHeaderHeight)];
  _content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [_view addSubview:_content];
  [self setPosition:_position];
}

- (void)setPosition:(NSString*)position {
  _position = [position copy];
  [_positionButtons enumerateKeysAndObjectsUsingBlock:^(
                        NSString* key, NSButton* button, BOOL*) {
    button.contentTintColor = [key isEqualToString:self->_position]
                                  ? NSColor.controlAccentColor
                                  : NSColor.secondaryLabelColor;
  }];
}

- (void)inspectAt:(NSPoint)point {
  if (_client) {
    _client->InspectAt(static_cast<int>(point.x), static_cast<int>(point.y));
  }
}

- (void)showPanel:(NSString*)panel {
  if (_client) {
    _client->ShowPanel(panel.UTF8String);
  }
}

- (void)enterInspectElementMode {
  if (_client) {
    _client->EnterInspectElementMode();
  }
}

- (void)close {
  if (!_client) {
    return;
  }
  if (_frontend) {
    _client->Close();
  } else {
    // Not created yet: nothing to close; tell the tab now.
    [self detach];
    [_delegate devToolsDockDidClose:self];
  }
}

- (void)detach {
  _delegate = nil;
  if (_frontend) {
    // Stay alive until CEF reports the browser closed (handleClosed).
    ClosingDocks().insert(self);
    _frontend->GetHost()->CloseBrowser(true);
    return;
  }
  if (_client) {
    _client->DetachDelegate();
  }
  _client = nullptr;
}

#pragma mark - Header buttons

- (void)closeClicked:(id)sender {
  [self close];
}

- (void)positionClicked:(NSButton*)sender {
  [_delegate devToolsDock:self wantsPosition:sender.identifier];
}

#pragma mark - DevTools browser events (UI thread)

- (void)handleCreated:(CefRefPtr<CefBrowser>)frontend {
  _frontend = frontend;
  NSView* view = (__bridge NSView*)frontend->GetHost()->GetWindowHandle();
  view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  view.frame = _content.bounds;
  _counted = YES;
  [_counter browserWindowDidCreateBrowser];
}

- (void)handleCloseReady {
  if (_frontend) {
    NSView* view = (__bridge NSView*)_frontend->GetHost()->GetWindowHandle();
    [view removeFromSuperview];
  }
}

- (void)handleClosed {
  _frontend = nullptr;
  if (_client) {
    _client->DetachDelegate();
  }
  _client = nullptr;
  if (_counted) {
    _counted = NO;
    [_counter browserWindowDidCloseBrowser];
  }
  [_delegate devToolsDockDidClose:self];
  ClosingDocks().erase(self);
}

- (BOOL)handleKeyEvent:(NSEvent*)event {
  return [_delegate devToolsDockHandleKeyEvent:event];
}

- (void)handleWantsClose {
  [self close];
}

- (void)handleOpenURL:(NSString*)url {
  [_delegate devToolsDock:self openURL:url];
}

@end
