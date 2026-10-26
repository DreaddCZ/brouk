#import "platform/mac/link_routing_controller.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cef/devtools_channel.h"
#include "core/link_unwrap.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"
#import "platform/mac/browser_tab.h"
#import "platform/mac/browser_window_controller.h"
#import "platform/mac/link_picker.h"
#import "platform/mac/link_resolver.h"

namespace {

NSString* ToNSString(const std::string& text) {
  return [NSString stringWithUTF8String:text.c_str()] ?: @"";
}

// The screen the user is looking at: for a link from another app the one
// with the mouse pointer (they just clicked it), else the source window's.
NSScreen* ScreenWithMouse() {
  NSPoint mouse = NSEvent.mouseLocation;
  for (NSScreen* screen in NSScreen.screens) {
    if (NSPointInRect(mouse, screen.frame)) {
      return screen;
    }
  }
  return NSScreen.mainScreen;
}

}  // namespace

// One link on its way to a tab.
@interface BRLinkRequest : NSObject
@property(nonatomic, copy) NSString* url;
@property(nonatomic) brouk::LinkSource source;
@property(nonatomic) brouk::ProfileId sourceProfile;
// "From Outlook" / "From Work".
@property(nonatomic, copy) NSString* sourceName;
@property(nonatomic, weak) BRTab* tab;
@property(nonatomic, weak) BRBrowserWindowController* window;
@property(nonatomic) BOOL foreground;
@property(nonatomic, nullable) NSScreen* screen;
// Set once the real URL is known: what routing uses, what the tab loads
// (the wrapper, so the company's check runs), and the wrapper's name.
@property(nonatomic, copy) NSString* realURL;
@property(nonatomic, copy) NSString* openURL;
@property(nonatomic, copy, nullable) NSString* wrapper;
@end

@implementation BRLinkRequest
@end

@implementation BRLinkRoutingController {
  __weak id<BRLinkRoutingHost> _host;
  std::filesystem::path _path;
  brouk::LinkRoutingConfig _config;
  std::vector<std::string> _problems;
  BRLinkPicker* _picker;
  BRLinkResolver* _resolver;
  // Links waiting while the picker is open.
  NSMutableArray<BRLinkRequest*>* _queue;
  // Screenshots of active tabs for the picker, while they are taken.
  std::vector<CefRefPtr<brouk::DevToolsChannel>> _thumbnailChannels;
}

- (instancetype)initWithHost:(id<BRLinkRoutingHost>)host
                  configFile:(NSString*)path {
  if ((self = [super init])) {
    _host = host;
    _path = path.fileSystemRepresentation;
    _picker = [[BRLinkPicker alloc] init];
    _resolver = [[BRLinkResolver alloc] initWithBrowserCounter:host];
    _queue = [NSMutableArray array];
    [self reload];
  }
  return self;
}

- (const brouk::LinkRoutingConfig&)config {
  return _config;
}

- (const std::vector<std::string>&)problems {
  return _problems;
}

- (void)reload {
  _problems.clear();
  _config = brouk::LoadLinkRouting(_path, _problems);
  for (const std::string& problem : _problems) {
    NSLog(@"Link routing: %s", problem.c_str());
  }
}

- (std::optional<brouk::Error>)setConfig:(brouk::LinkRoutingConfig)config {
  if (std::optional<brouk::Error> error =
          brouk::SaveLinkRouting(_path, config)) {
    return error;
  }
  _config = std::move(config);
  _problems.clear();
  return std::nullopt;
}

#pragma mark - Entry points

- (void)openExternalURL:(NSString*)url fromApp:(NSString*)appName {
  BRLinkRequest* request = [[BRLinkRequest alloc] init];
  request.url = url;
  request.source = brouk::LinkSource::kOtherApp;
  request.sourceName = appName.length
                           ? [NSString stringWithFormat:@"From %@", appName]
                           : @"From another app";
  request.foreground = YES;
  request.screen = ScreenWithMouse();
  [self enqueue:request];
}

- (BOOL)routeURL:(NSString*)url
          source:(brouk::LinkSource)source
         fromTab:(BRTab*)tab
        inWindow:(BRBrowserWindowController*)controller
      foreground:(BOOL)foreground {
  id<BRLinkRoutingHost> host = _host;
  BRLinkRequest* request = [[BRLinkRequest alloc] init];
  request.url = url;
  request.source = source;
  request.sourceProfile = tab.profileId;
  request.sourceName =
      [NSString stringWithFormat:@"From %s",
                                 [host profileInfo:tab.profileId].name.c_str()];
  request.tab = tab;
  request.window = controller;
  request.foreground = foreground;
  request.screen = controller.window.screen;

  // Decided here, so the caller knows whether to open the link itself.
  brouk::UnwrappedLink link =
      brouk::UnwrapLink(url.UTF8String ?: "", _config.wrappers);
  if (link.kind != brouk::UnwrappedLink::Kind::kOpaque) {
    brouk::ProfileDecision decision = [self decideProfileFor:request
                                                     realURL:link.url];
    if (decision.stays_in_source) {
      return NO;
    }
  }
  [self enqueue:request];
  return YES;
}

- (void)cancelPending {
  [_queue removeAllObjects];
  [_resolver cancelAll];
  [_picker cancel];
}

#pragma mark - Routing

- (void)enqueue:(BRLinkRequest*)request {
  if (_picker.visible) {
    [_queue addObject:request];
    return;
  }
  [self process:request];
}

// The picker closed: the next waiting link.
- (void)processNext {
  while (_queue.count > 0 && !_picker.visible) {
    BRLinkRequest* next = _queue.firstObject;
    [_queue removeObjectAtIndex:0];
    if (next.realURL) {
      [self route:next];
    } else {
      [self process:next];
    }
  }
}

- (void)process:(BRLinkRequest*)request {
  brouk::UnwrappedLink link =
      brouk::UnwrapLink(request.url.UTF8String ?: "", _config.wrappers);
  request.wrapper = link.wrapper.empty() ? nil : ToNSString(link.wrapper);
  if (link.kind == brouk::UnwrappedLink::Kind::kOpaque) {
    // Followed in a neutral context; the final URL is routed like a link
    // from the original source (and loaded directly: the check has run).
    __weak BRLinkRoutingController* weakSelf = self;
    std::vector<brouk::LinkWrapperRule> wrappers = _config.wrappers;
    [_resolver resolveURL:request.url
                  wrapper:request.wrapper ?: @""
             userWrappers:std::move(wrappers)
                 onScreen:request.screen
                     done:^(NSString* _Nullable finalURL) {
                       if (!finalURL) {
                         return;
                       }
                       request.realURL = finalURL;
                       request.openURL = finalURL;
                       [weakSelf enqueueResolved:request];
                     }];
    return;
  }
  request.realURL = ToNSString(link.url);
  request.openURL = request.url;
  [self route:request];
}

- (void)enqueueResolved:(BRLinkRequest*)request {
  if (_picker.visible) {
    [_queue addObject:request];
    return;
  }
  [self route:request];
}

- (brouk::ProfileDecision)decideProfileFor:(BRLinkRequest*)request
                                   realURL:(const std::string&)url {
  id<BRLinkRoutingHost> host = _host;
  std::vector<brouk::ProfileId> existing;
  for (const brouk::ProfileInfo& info : [host profileList]) {
    existing.push_back(info.id);
  }
  return brouk::DecideProfile(_config, url, request.source,
                              request.sourceProfile, [host defaultProfileId],
                              existing);
}

- (void)route:(BRLinkRequest*)request {
  brouk::ProfileDecision decision =
      [self decideProfileFor:request realURL:request.realURL.UTF8String ?: ""];
  [self logRouting:request decision:decision];
  if (decision.stays_in_source) {
    [self openInSource:request];
    return;
  }
  if (decision.profiles.size() == 1) {
    brouk::WindowDecision windows =
        [self decideWindowFor:decision.profiles.front() request:request];
    if (windows.kind != brouk::WindowDecision::Kind::kChoose) {
      [self open:request profile:decision.profiles.front() windows:windows];
      return;
    }
  }
  [self showPickerFor:request decision:decision];
}

// One line in the macOS log per routed link, so a link that opens in an
// unexpected profile can be explained (Console, process "brouk"). Never
// query values: LinkForLog leaves them out.
- (void)logRouting:(BRLinkRequest*)request
          decision:(const brouk::ProfileDecision&)decision {
  std::string profiles;
  for (const brouk::ProfileId& profile : decision.profiles) {
    profiles += (profiles.empty() ? "" : ", ") + profile;
  }
  std::string rule = decision.rule_pattern ? "rule " + *decision.rule_pattern
                                           : std::string("no rule");
  NSLog(@"Link routing: %@: %s, via %@, routed by %s: %s -> %s",
        request.sourceName,
        brouk::LinkForLog(request.url.UTF8String ?: "").c_str(),
        request.wrapper ?: @"no wrapper",
        brouk::LinkForLog(request.realURL.UTF8String ?: "").c_str(),
        rule.c_str(), profiles.c_str());
}

- (std::vector<brouk::RoutingWindow>)
    routingWindowsOn:(nullable NSScreen*)screen
         controllers:(NSArray<BRBrowserWindowController*>*)controllers {
  std::vector<brouk::RoutingWindow> windows;
  for (BRBrowserWindowController* controller in controllers) {
    brouk::RoutingWindow window;
    window.id = controller.windowId;
    window.profiles = [controller groupProfiles];
    window.on_current_screen = screen && controller.window.screen == screen;
    window.on_active_space = controller.window.isOnActiveSpace;
    windows.push_back(std::move(window));
  }
  return windows;
}

- (NSArray<BRBrowserWindowController*>*)normalWindows {
  id<BRLinkRoutingHost> host = _host;
  NSMutableArray<BRBrowserWindowController*>* result = [NSMutableArray array];
  for (BRBrowserWindowController* controller in
       [host browserWindowControllers]) {
    if (!controller.isPopup && controller.window.isVisible) {
      [result addObject:controller];
    }
  }
  // Minimized windows can still be chosen, after the visible ones.
  for (BRBrowserWindowController* controller in
       [host browserWindowControllers]) {
    if (!controller.isPopup && controller.window.isMiniaturized) {
      [result addObject:controller];
    }
  }
  return result;
}

- (brouk::WindowDecision)decideWindowFor:(const brouk::ProfileId&)profile
                                 request:(BRLinkRequest*)request {
  id<BRLinkRoutingHost> host = _host;
  std::vector<brouk::RoutingWindow> windows =
      [self routingWindowsOn:request.screen controllers:[self normalWindows]];
  return brouk::DecideWindow(profile, [host defaultProfileId], windows);
}

- (nullable BRBrowserWindowController*)windowWithId:(std::int64_t)windowId {
  for (BRBrowserWindowController* controller in [self normalWindows]) {
    if (controller.windowId == windowId) {
      return controller;
    }
  }
  return nil;
}

#pragma mark - Picker

- (void)showPickerFor:(BRLinkRequest*)request
             decision:(const brouk::ProfileDecision&)decision {
  id<BRLinkRoutingHost> host = _host;
  NSMutableArray<BRLinkPickerProfile*>* profiles = [NSMutableArray array];
  for (const brouk::ProfileId& id : decision.profiles) {
    brouk::ProfileInfo info = [host profileInfo:id];
    BRLinkPickerProfile* profile = [[BRLinkPickerProfile alloc] init];
    profile.profileId = id;
    profile.name = ToNSString(info.name);
    profile.color = info.color;
    [profiles addObject:profile];
  }
  NSString* pattern =
      decision.rule_pattern ? ToNSString(*decision.rule_pattern) : nil;

  __weak BRLinkRoutingController* weakSelf = self;
  auto windowsForProfile =
      ^NSArray<BRLinkPickerWindow*>*(const brouk::ProfileId& profile) {
    return [weakSelf pickerWindowsFor:profile request:request];
  };
  // A link from another app brings Brouk to the front first.
  [NSApp activateIgnoringOtherApps:YES];
  [_picker showURL:request.realURL
                    via:request.wrapper
                   from:request.sourceName
               profiles:profiles
            rulePattern:pattern
      windowsForProfile:windowsForProfile
               onScreen:request.screen
                   done:^(const BRLinkPickerResult& result) {
                     [weakSelf pickerDone:result
                                  request:request
                                  pattern:pattern];
                   }];
}

// The windows to offer for `profile`, or nil when routing decides alone.
- (nullable NSArray<BRLinkPickerWindow*>*)
    pickerWindowsFor:(const brouk::ProfileId&)profile
             request:(BRLinkRequest*)request {
  brouk::WindowDecision decision = [self decideWindowFor:profile
                                                 request:request];
  if (decision.kind != brouk::WindowDecision::Kind::kChoose) {
    return nil;
  }
  id<BRLinkRoutingHost> host = _host;
  NSMutableArray<BRLinkPickerWindow*>* windows = [NSMutableArray array];
  for (std::int64_t windowId : decision.choices) {
    BRBrowserWindowController* controller = [self windowWithId:windowId];
    if (!controller) {
      continue;
    }
    BRLinkPickerWindow* entry = [[BRLinkPickerWindow alloc] init];
    entry.windowId = windowId;
    BRTab* active = [controller activeTab];
    BOOL blank =
        active.url.length == 0 || [active.url isEqualToString:@"about:blank"];
    entry.title =
        blank ? @"New Tab" : (active.title.length ? active.title : active.url);
    entry.favicon = active.favicon;
    NSMutableArray<BRLinkPickerGroup*>* groups = [NSMutableArray array];
    for (const brouk::TabStripModel::Group& group : [controller tabGroups]) {
      if (group.count == 0) {
        continue;
      }
      brouk::ProfileInfo info = [host profileInfo:group.profile];
      BRLinkPickerGroup* chip = [[BRLinkPickerGroup alloc] init];
      chip.profileId = group.profile;
      chip.name = ToNSString(info.name);
      chip.color = info.color;
      chip.tabCount = group.count;
      [groups addObject:chip];
    }
    entry.groups = groups;
    [windows addObject:entry];
    [self requestThumbnailOf:active forWindow:windowId];
  }
  return windows;
}

// The active tab as the browser rendered it (DevTools protocol), so no
// screen recording permission is needed. A tab with nothing rendered keeps
// its favicon.
- (void)requestThumbnailOf:(nullable BRTab*)tab
                 forWindow:(std::int64_t)windowId {
  CefRefPtr<CefBrowser> browser = tab ? [tab browser] : nullptr;
  if (!browser) {
    return;
  }
  CefRefPtr<brouk::DevToolsChannel> channel =
      new brouk::DevToolsChannel(browser);
  _thumbnailChannels.push_back(channel);
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("format", "jpeg");
  params->SetInt("quality", 60);
  params->SetBool("optimizeForSpeed", true);
  __weak BRLinkRoutingController* weakSelf = self;
  brouk::DevToolsChannel* raw = channel.get();
  channel->Send(
      "Page.captureScreenshot", params,
      [weakSelf, raw, windowId](CefRefPtr<CefDictionaryValue> result,
                                const std::string& /*error*/) {
        BRLinkRoutingController* strongSelf = weakSelf;
        if (!strongSelf) {
          return;
        }
        [strongSelf thumbnailChannelDone:raw];
        if (!result || !result->HasKey("data")) {
          return;
        }
        NSData* data = [[NSData alloc]
            initWithBase64EncodedString:ToNSString(result->GetString("data")
                                                       .ToString())
                                options:0];
        NSImage* image = data ? [[NSImage alloc] initWithData:data] : nil;
        if (image) {
          [strongSelf->_picker setThumbnail:image forWindow:windowId];
        }
      });
}

- (void)thumbnailChannelDone:(brouk::DevToolsChannel*)channel {
  for (auto it = _thumbnailChannels.begin(); it != _thumbnailChannels.end();
       ++it) {
    if (it->get() == channel) {
      // Not while the channel is still delivering this answer.
      CefRefPtr<brouk::DevToolsChannel> done = *it;
      _thumbnailChannels.erase(it);
      dispatch_async(dispatch_get_main_queue(), ^{
        done->Close();
      });
      return;
    }
  }
}

- (void)pickerDone:(const BRLinkPickerResult&)result
           request:(BRLinkRequest*)request
           pattern:(nullable NSString*)pattern {
  for (const CefRefPtr<brouk::DevToolsChannel>& channel : _thumbnailChannels) {
    channel->Close();
  }
  _thumbnailChannels.clear();
  if (!result.cancelled) {
    if (result.always && pattern.length) {
      brouk::LinkRoutingConfig config = _config;
      if (brouk::NarrowRoutingRule(config, pattern.UTF8String,
                                   result.profile)) {
        if (std::optional<brouk::Error> error = [self setConfig:config]) {
          NSLog(@"Link routing: %s", error->message.c_str());
        }
      }
    }
    brouk::WindowDecision windows;
    switch (result.window) {
      case BRLinkPickerResult::Window::kNotAsked:
        windows = [self decideWindowFor:result.profile request:request];
        if (windows.kind == brouk::WindowDecision::Kind::kChoose) {
          // Windows changed while the picker was open: the most recent.
          windows.kind = brouk::WindowDecision::Kind::kWindow;
          windows.window = windows.choices.front();
        }
        break;
      case BRLinkPickerResult::Window::kExisting:
        windows.kind = brouk::WindowDecision::Kind::kWindow;
        windows.window = result.window_id;
        break;
      case BRLinkPickerResult::Window::kNewWindow:
        windows.kind = brouk::WindowDecision::Kind::kNewWindow;
        break;
    }
    [self open:request profile:result.profile windows:windows];
  }
  [self processNext];
}

#pragma mark - Opening

// The link stays in its source tab's profile, as without routing.
- (void)openInSource:(BRLinkRequest*)request {
  BRBrowserWindowController* window = request.window;
  BRTab* tab = request.tab;
  if (!window) {
    // The source window is gone: its profile's group decides.
    [self open:request
        profile:request.sourceProfile
        windows:[self decideWindowFor:request.sourceProfile request:request]];
    return;
  }
  if (request.source == brouk::LinkSource::kTypedNewTab && tab) {
    [window loadTypedURL:request.openURL inTab:tab];
    return;
  }
  [window openURL:request.openURL
        nextToTab:tab
          profile:request.sourceProfile
       foreground:request.foreground];
}

- (void)open:(BRLinkRequest*)request
     profile:(const brouk::ProfileId&)profile
     windows:(const brouk::WindowDecision&)windows {
  id<BRLinkRoutingHost> host = _host;
  BRBrowserWindowController* target = nil;
  switch (windows.kind) {
    case brouk::WindowDecision::Kind::kWindow:
    case brouk::WindowDecision::Kind::kNewGroup:
      target = [self windowWithId:windows.window];
      break;
    case brouk::WindowDecision::Kind::kChoose:
      target = [self windowWithId:windows.choices.front()];
      break;
    case brouk::WindowDecision::Kind::kNewWindow:
      break;
  }
  BOOL newWindow = target == nil;
  if (newWindow) {
    target = [host openWindow];
  }
  BRBrowserWindowController* source = request.window;
  // A background tab (Cmd-click) stays in the background in its own
  // window; anywhere else it is shown, so it can be found.
  BOOL activateTab = request.foreground || target != source;
  [target openTabWithURL:request.openURL profile:profile activate:activateTab];
  if (request.foreground || newWindow) {
    [self bringToFront:target];
  }
  if (request.source == brouk::LinkSource::kTypedNewTab && source &&
      request.tab) {
    [source closeTabIfUnused:request.tab];
  }
}

// Brings `controller`'s window to the front. On another Space macOS
// switches there, or (setting) the window moves to the current Space.
- (void)bringToFront:(BRBrowserWindowController*)controller {
  id<BRLinkRoutingHost> host = _host;
  NSWindow* window = controller.window;
  [NSApp activateIgnoringOtherApps:YES];
  if (!window.isOnActiveSpace && [host moveChosenWindowToActiveSpace]) {
    NSWindowCollectionBehavior behavior = window.collectionBehavior;
    window.collectionBehavior =
        behavior | NSWindowCollectionBehaviorMoveToActiveSpace;
    [window makeKeyAndOrderFront:nil];
    dispatch_async(dispatch_get_main_queue(), ^{
      window.collectionBehavior = behavior;
    });
    return;
  }
  [window makeKeyAndOrderFront:nil];
}

@end
