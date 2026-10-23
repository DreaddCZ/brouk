#import "platform/mac/devtools_dock.h"

#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "cef/devtools_channel.h"
#include "cef/devtools_frontend_client.h"
#include "cef/devtools_tools_handler.h"
#include "cef/internal_page_client.h"
#include "core/devtools_frontend.h"
#include "include/wrapper/cef_helpers.h"
#import "platform/mac/devtools_window.h"
#import "platform/mac/menu_shortcuts.h"

@interface BRDevToolsDock () <BRDevToolsWindowOwner>
- (void)handleCreated:(CefRefPtr<CefBrowser>)frontend;
- (void)handleCloseReady;
- (void)handleClosed;
- (BOOL)handleKeyEvent:(NSEvent*)event;
- (void)handleWantsClose;
- (void)handleOpenURL:(NSString*)url;
- (void)handleToolsCreated:(CefRefPtr<CefBrowser>)tools;
- (void)handleToolsCloseReady;
- (void)handleToolsClosed;
- (void)inspectToolsAt:(NSPoint)point;
- (void)saveFile:(brouk::ToolsHost::File)main
           extra:(std::optional<brouk::ToolsHost::File>)extra
            done:(brouk::ToolsHost::Done)done;
- (brouk::ToolsHost::Rules)requestRulesFromDelegate;
- (std::optional<brouk::Error>)storeRequestRules:(brouk::RequestRuleSet)rules;
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

// Our own tools' page (brouk://tools) in the dock.
class DockToolsBridge : public InternalPageDelegate {
 public:
  explicit DockToolsBridge(BRDevToolsDock* dock) : dock_(dock) {}
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override {
    [dock_ handleToolsCreated:browser];
  }
  void OnCloseReady() override { [dock_ handleToolsCloseReady]; }
  void OnBrowserClosed() override { [dock_ handleToolsClosed]; }
  void OnTitleChanged(const std::string& /*title*/) override {}
  bool OnUnhandledKeyEvent(const CefKeyEvent& event,
                           CefEventHandle os_event) override {
    NSEvent* ns_event = (__bridge NSEvent*)os_event;
    return ns_event && event.type == KEYEVENT_RAWKEYDOWN &&
           ns_event.type == NSEventTypeKeyDown &&
           [dock_ handleKeyEvent:ns_event];
  }
  void OnInspectElement(int x, int y) override {
    [dock_ inspectToolsAt:NSMakePoint(x, y)];
  }

 private:
  __weak BRDevToolsDock* dock_;
};

class DockToolsHost : public ToolsHost {
 public:
  explicit DockToolsHost(BRDevToolsDock* dock) : dock_(dock) {}
  void SaveFiles(File main, std::optional<File> extra, Done done) override {
    BRDevToolsDock* dock = dock_;
    if (!dock) {
      done(false, "The developer tools have closed.");
      return;
    }
    [dock saveFile:std::move(main) extra:std::move(extra) done:std::move(done)];
  }
  Rules RequestRules() override {
    BRDevToolsDock* dock = dock_;
    return dock ? [dock requestRulesFromDelegate] : Rules{};
  }
  std::optional<Error> SetRequestRules(RequestRuleSet rules) override {
    BRDevToolsDock* dock = dock_;
    if (!dock) {
      return Error{"The developer tools have closed."};
    }
    return [dock storeRequestRules:std::move(rules)];
  }
  std::string SecretHeaderNames() override {
    BRDevToolsDock* dock = dock_;
    return dock ? std::string(dock.secretHeaderNames.UTF8String)
                : std::string();
  }

 private:
  __weak BRDevToolsDock* dock_;
};

}  // namespace brouk

namespace {

// The dock's tabs: Chromium DevTools, then our own tools (brouk://tools/#id).
struct DockTool {
  NSString* id;
  NSString* title;
};
const DockTool kDockTools[] = {
    {@"devtools", @"DevTools"}, {@"site-data", @"Site data"},
    {@"requests", @"Requests"}, {@"tokens", @"Tokens"},
    {@"rules", @"Rules"},
};

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
  // Our own tools (006): their page, shown instead of DevTools.
  CefRefPtr<CefRequestContext> _context;
  CefRefPtr<brouk::DevToolsChannel> _channel;
  std::unique_ptr<brouk::DockToolsBridge> _toolsBridge;
  std::unique_ptr<brouk::DockToolsHost> _toolsHost;
  CefRefPtr<brouk::InternalPageClient> _toolsClient;
  CefRefPtr<CefBrowser> _tools;
  BOOL _toolsCounted;
  BOOL _toolsCreating;
  NSView* _toolsContent;
  NSSegmentedControl* _tabs;
  NSString* _pendingTool;
  BOOL _closing;
  BOOL _frontendCreating;
  BOOL _ownTools;
  brouk::DevToolsPreferences* _preferences;  // Not owned; the app's.
  // DevTools for the tools page itself, to develop the tools.
  BRDevToolsWindowController* _toolsInspector;
}

- (instancetype)initWithInspectedBrowser:(CefRefPtr<CefBrowser>)inspected
                                 inspect:(NSPoint)point
                                 context:(CefRefPtr<CefRequestContext>)context
                             preferences:
                                 (brouk::DevToolsPreferences*)preferences
                                ownTools:(BOOL)ownTools
                                 counter:(id<BRBrowserCounter>)counter
                                delegate:(id<BRDevToolsDockDelegate>)delegate {
  CEF_REQUIRE_UI_THREAD();
  if ((self = [super init])) {
    _delegate = delegate;
    _counter = counter;
    _position = @"bottom";
    _context = context;
    _preferences = preferences;
    _ownTools = ownTools;
    _channel = new brouk::DevToolsChannel(inspected);
    if (ownTools) {
      // Dev tools on: record the tab's requests (006).
      _channel->StartRecording();
      _channel->WatchServiceWorkers();
    }
    _toolsHost = std::make_unique<brouk::DockToolsHost>(self);
    _frontendCreating = YES;
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
  if (!_ownTools) {
    // DevTools only: it fills the view.
    _content = [[NSView alloc] initWithFrame:_view.bounds];
    _content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [_view addSubview:_content];
    return;
  }
  BRDevToolsHeaderView* header = [[BRDevToolsHeaderView alloc]
      initWithFrame:NSMakeRect(0, 0, 600, kHeaderHeight)];
  header.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
  header.frame = NSMakeRect(0, 300 - kHeaderHeight, 600, kHeaderHeight);

  // Tool tabs: Chromium DevTools and our own tools (006).
  NSMutableArray<NSString*>* titles = [NSMutableArray array];
  for (const DockTool& tool : kDockTools) {
    [titles addObject:tool.title];
  }
  _tabs = [NSSegmentedControl
      segmentedControlWithLabels:titles
                    trackingMode:NSSegmentSwitchTrackingSelectOne
                          target:self
                          action:@selector(toolClicked:)];
  _tabs.controlSize = NSControlSizeSmall;
  _tabs.font = [NSFont systemFontOfSize:11];
  _tabs.selectedSegment = 0;
  _tabs.accessibilityLabel = @"Developer tools";
  [_tabs sizeToFit];
  _tabs.frame = NSMakeRect(8, 3, _tabs.frame.size.width, 22);
  [header addSubview:_tabs];

  NSMutableDictionary* buttons = [NSMutableDictionary dictionary];
  NSArray* positions = @[
    @[ @"bottom", @"rectangle.bottomthird.inset.filled", @"Dock to bottom" ],
    @[ @"left", @"rectangle.leftthird.inset.filled", @"Dock to left" ],
    @[ @"right", @"rectangle.rightthird.inset.filled", @"Dock to right" ],
    @[ @"window", @"macwindow.on.rectangle", @"Open in a separate window" ],
  ];
  CGFloat x = 600 - 28;
  NSButton* close = HeaderButton(
      @"xmark", brouk::mac::ToolTip(@"Close developer tools", "devtools"), self,
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
  _toolsContent =
      [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 600, 300 - kHeaderHeight)];
  _toolsContent.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  _toolsContent.hidden = YES;
  [_view addSubview:_toolsContent];
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
  if (_closing) {
    return;
  }
  _closing = YES;
  _channel->Close();
  [_toolsInspector detach];
  _toolsInspector = nil;
  if (_tools) {
    _tools->GetHost()->CloseBrowser(true);
  }
  if (_frontend) {
    _client->Close();
  }
  [self finishClosingIfDone];
}

- (void)detach {
  _delegate = nil;
  if (_frontend || _frontendCreating || _tools || _toolsCreating) {
    // Stay alive until CEF reports the browsers closed.
    ClosingDocks().insert(self);
  }
  [self close];
}

// Both browsers are gone (or were never created): the dock is closed.
- (void)finishClosingIfDone {
  if (!_closing || _frontend || _frontendCreating || _tools || _toolsCreating) {
    return;
  }
  if (_client) {
    _client->DetachDelegate();
  }
  _client = nullptr;
  _toolsClient = nullptr;
  _channel = nullptr;
  id<BRDevToolsDockDelegate> delegate = _delegate;
  _delegate = nil;
  [delegate devToolsDockDidClose:self];
  ClosingDocks().erase(self);
}

#pragma mark - Request rules

- (void)setRequestRules:(const brouk::RequestRuleSet&)rules {
  if (!_channel || !_ownTools) {
    return;
  }
  __weak BRDevToolsDock* weakSelf = self;
  _channel->SetRules(rules, [weakSelf] {
    BRDevToolsDock* strongSelf = weakSelf;
    if (strongSelf) {
      [strongSelf->_delegate devToolsDockRulesAffectedPage:strongSelf];
    }
  });
}

- (brouk::ToolsHost::Rules)requestRulesFromDelegate {
  id<BRDevToolsDockDelegate> delegate = _delegate;
  return delegate ? [delegate devToolsDockRequestRules:self]
                  : brouk::ToolsHost::Rules{};
}

- (std::optional<brouk::Error>)storeRequestRules:(brouk::RequestRuleSet)rules {
  id<BRDevToolsDockDelegate> delegate = _delegate;
  if (!delegate) {
    return brouk::Error{"The developer tools have closed."};
  }
  return [delegate devToolsDock:self setRequestRules:std::move(rules)];
}

#pragma mark - Our own tools

- (void)toolClicked:(NSSegmentedControl*)sender {
  NSInteger index = sender.selectedSegment;
  if (index < 0 || index >= static_cast<NSInteger>(std::size(kDockTools))) {
    return;
  }
  [self showTool:kDockTools[index].id];
}

- (void)showTool:(NSString*)tool {
  BOOL devtools = [tool isEqualToString:@"devtools"];
  _content.hidden = !devtools;
  _toolsContent.hidden = devtools;
  if (devtools || _closing) {
    return;
  }
  std::string url = std::string("brouk://tools/#") + tool.UTF8String;
  if (_tools) {
    _tools->GetMainFrame()->LoadURL(url);
    return;
  }
  _pendingTool = tool;
  if (_toolsCreating) {
    return;
  }
  _toolsCreating = YES;
  _toolsBridge = std::make_unique<brouk::DockToolsBridge>(self);
  _toolsClient = new brouk::InternalPageClient(
      _toolsBridge.get(), std::make_unique<brouk::DevToolsToolsHandler>(
                              _channel, _toolsHost.get()));
  NSRect bounds = _toolsContent.bounds;
  CefWindowInfo info;
  info.SetAsChild((__bridge void*)_toolsContent,
                  CefRect(0, 0, static_cast<int>(bounds.size.width),
                          static_cast<int>(bounds.size.height)));
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserHost::CreateBrowser(info, _toolsClient, url, CefBrowserSettings(),
                                nullptr, _context);
}

// Saves exported files (Postman collection, environment) where the user
// chooses; `extra` goes next to the main file.
- (void)saveFile:(brouk::ToolsHost::File)main
           extra:(std::optional<brouk::ToolsHost::File>)extra
            done:(brouk::ToolsHost::Done)done {
  NSSavePanel* panel = [NSSavePanel savePanel];
  panel.nameFieldStringValue = @(main.name.c_str());
  panel.message =
      extra ? [NSString stringWithFormat:@"“%s” is saved in the same folder.",
                                         extra->name.c_str()]
            : @"";
  __block brouk::ToolsHost::Done answer = std::move(done);
  __block brouk::ToolsHost::File mainFile = std::move(main);
  __block std::optional<brouk::ToolsHost::File> extraFile = std::move(extra);
  void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
    if (response != NSModalResponseOK || !panel.URL) {
      answer(false, "");
      return;
    }
    auto write = [](NSURL* url, const std::string& content) -> NSString* {
      NSData* data = [NSData dataWithBytes:content.data()
                                    length:content.size()];
      NSError* error = nil;
      return
          [data writeToURL:url options:NSDataWritingAtomic error:&error]
              ? nil
              : (error.localizedDescription ?: @"The file could not be saved.");
    };
    NSString* error = write(panel.URL, mainFile.content);
    if (!error && extraFile) {
      // A plain file name only, next to the main file.
      NSString* name = @(extraFile->name.c_str()).lastPathComponent;
      NSURL* url = [panel.URL.URLByDeletingLastPathComponent
          URLByAppendingPathComponent:name];
      error = write(url, extraFile->content);
    }
    answer(!error, error ? std::string(error.UTF8String) : std::string());
  };
  NSWindow* window = _view.window;
  if (window) {
    [panel beginSheetModalForWindow:window completionHandler:completion];
  } else {
    completion([panel runModal]);
  }
}

- (void)handleToolsCreated:(CefRefPtr<CefBrowser>)tools {
  _toolsCreating = NO;
  _tools = tools;
  NSView* view = (__bridge NSView*)tools->GetHost()->GetWindowHandle();
  view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  view.frame = _toolsContent.bounds;
  _toolsCounted = YES;
  [_counter browserWindowDidCreateBrowser];
  if (_closing) {
    tools->GetHost()->CloseBrowser(true);
  }
}

// "Inspect" in the tools page: DevTools for the page itself (006).
- (void)inspectToolsAt:(NSPoint)point {
  if (_toolsInspector) {
    [_toolsInspector inspectAt:point];
    return;
  }
  if (!_tools || _closing) {
    return;
  }
  _toolsInspector = [[BRDevToolsWindowController alloc]
      initWithInspectedBrowser:_tools
                         title:@"Brouk tools"
                       inspect:point
                       context:_context
                   preferences:_preferences
                       counter:_counter
                         owner:self];
  [_toolsInspector showWindow:nil];
}

- (void)devToolsWindowDidClose:(BRDevToolsWindowController*)controller {
  if (controller == _toolsInspector) {
    _toolsInspector = nil;
  }
}

- (void)handleToolsCloseReady {
  if (_tools) {
    NSView* view = (__bridge NSView*)_tools->GetHost()->GetWindowHandle();
    [view removeFromSuperview];
  }
}

- (void)handleToolsClosed {
  _tools = nullptr;
  _toolsCreating = NO;
  if (_toolsClient) {
    _toolsClient->DetachDelegate();
  }
  if (_toolsCounted) {
    _toolsCounted = NO;
    [_counter browserWindowDidCloseBrowser];
  }
  [self finishClosingIfDone];
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
  _frontendCreating = NO;
  _frontend = frontend;
  NSView* view = (__bridge NSView*)frontend->GetHost()->GetWindowHandle();
  view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  view.frame = _content.bounds;
  _counted = YES;
  [_counter browserWindowDidCreateBrowser];
  if (_closing) {
    _client->Close();
  }
}

- (void)handleCloseReady {
  if (_frontend) {
    NSView* view = (__bridge NSView*)_frontend->GetHost()->GetWindowHandle();
    [view removeFromSuperview];
  }
}

- (void)handleClosed {
  _frontend = nullptr;
  if (_counted) {
    _counted = NO;
    [_counter browserWindowDidCloseBrowser];
  }
  // DevTools closing (its own close button or the tab) closes the dock.
  [self close];
  [self finishClosingIfDone];
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
