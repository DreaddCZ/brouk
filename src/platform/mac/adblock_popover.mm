#import "platform/mac/adblock_popover.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "core/adblock_lists.h"

namespace {

constexpr CGFloat kWidth = 340;

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

NSTextField* Small(NSString* text) {
  NSTextField* label = [NSTextField labelWithString:text];
  label.font = [NSFont systemFontOfSize:11];
  label.textColor = NSColor.secondaryLabelColor;
  return label;
}

// The kind of a blocking list, for the counts.
enum class Kind { kAds, kTrackers, kOther };
Kind KindOf(const std::string& list_id) {
  const brouk::FilterListDef* list = brouk::FindBuiltinFilterList(list_id);
  if (list == nullptr) {
    return Kind::kOther;
  }
  switch (list->category) {
    case brouk::FilterCategory::kAds:
    case brouk::FilterCategory::kRegional:
      return Kind::kAds;
    case brouk::FilterCategory::kPrivacy:
      return Kind::kTrackers;
    default:
      return Kind::kOther;
  }
}

struct DomainStats {
  int requests = 0;
  int blocked = 0;
};

}  // namespace

@interface BRAdblockPopoverController : NSViewController
@property(nonatomic, weak) NSPopover* popover;
@end

@implementation BRAdblockPopoverController {
  __weak id<BRAdblockPopoverActions> _actions;
}

- (instancetype)initWithActions:(id<BRAdblockPopoverActions>)actions {
  if ((self = [super initWithNibName:nil bundle:nil])) {
    _actions = actions;
  }
  return self;
}

- (void)loadView {
  id<BRAdblockPopoverActions> actions = _actions;
  NSString* host = [actions adblockPageHost];
  const std::vector<brouk::AdblockRequestInfo>& requests =
      [actions adblockPageRequests];

  // The switch for the site.
  NSTextField* site = [NSTextField labelWithString:host.length ? host : @"—"];
  site.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize + 1];
  site.lineBreakMode = NSLineBreakByTruncatingMiddle;
  NSSwitch* toggle = [[NSSwitch alloc] init];
  toggle.state = [actions adblockSiteEnabled] ? NSControlStateValueOn
                                              : NSControlStateValueOff;
  toggle.target = self;
  toggle.action = @selector(toggled:);
  toggle.accessibilityLabel = @"Block ads and trackers on this site";
  BOOL profileOn = [actions adblockProfileEnabled];
  toggle.enabled = profileOn && host.length > 0;
  NSStackView* header = [NSStackView stackViewWithViews:@[ site, toggle ]];
  header.distribution = NSStackViewDistributionEqualCentering;
  [header.widthAnchor constraintEqualToConstant:kWidth].active = YES;
  NSTextField* state =
      Small(!profileOn ? @"Blocking is off for this profile (Blocker settings)."
            : toggle.state == NSControlStateValueOn
                ? @"Ads and trackers are blocked on this site."
                : @"Blocking is off for this site.");

  // What was blocked.
  int ads = 0;
  int trackers = 0;
  int other = 0;
  std::map<std::string, DomainStats> domains;
  for (const brouk::AdblockRequestInfo& info : requests) {
    if (info.blocked) {
      switch (KindOf(info.list_id)) {
        case Kind::kAds:
          ++ads;
          break;
        case Kind::kTrackers:
          ++trackers;
          break;
        case Kind::kOther:
          ++other;
          break;
      }
    }
    if (info.third_party && !info.host.empty()) {
      DomainStats& stats = domains[info.host];
      ++stats.requests;
      stats.blocked += info.blocked ? 1 : 0;
    }
  }
  NSTextField* counts = [NSTextField
      labelWithString:[NSString
                          stringWithFormat:@"Blocked: %d ads · %d trackers · "
                                           @"%d other",
                                           ads, trackers, other]];

  // Third-party domains, most requests first.
  std::vector<std::pair<std::string, DomainStats>> sorted(domains.begin(),
                                                          domains.end());
  std::ranges::sort(sorted, [](const auto& a, const auto& b) {
    return a.second.requests > b.second.requests;
  });
  NSStackView* rows = [[NSStackView alloc] init];
  rows.orientation = NSUserInterfaceLayoutOrientationVertical;
  rows.alignment = NSLayoutAttributeLeading;
  rows.spacing = 4;
  for (const auto& [domain, stats] : sorted) {
    NSTextField* name = [NSTextField labelWithString:ToNSString(domain)];
    name.lineBreakMode = NSLineBreakByTruncatingMiddle;
    name.textColor =
        stats.blocked > 0 ? NSColor.systemRedColor : NSColor.labelColor;
    NSTextField* numbers =
        Small(stats.blocked > 0
                  ? [NSString stringWithFormat:@"%d (%d blocked)",
                                               stats.requests, stats.blocked]
                  : [NSString stringWithFormat:@"%d", stats.requests]);
    NSPopUpButton* rule = [[NSPopUpButton alloc] initWithFrame:NSZeroRect
                                                     pullsDown:YES];
    rule.bezelStyle = NSBezelStyleInline;
    rule.controlSize = NSControlSizeSmall;
    [rule addItemWithTitle:@"Rule"];
    NSString* d = ToNSString(domain);
    for (NSArray<NSString*>* item in @[
           @[ @"Block on this site", @"block-site" ],
           @[ @"Block everywhere", @"block" ],
           @[ @"Allow on this site", @"allow-site" ],
           @[ @"Allow everywhere", @"allow" ]
         ]) {
      [rule addItemWithTitle:item[0]];
      rule.lastItem.representedObject = @[ item[1], d ];
      rule.lastItem.target = self;
      rule.lastItem.action = @selector(addRule:);
    }
    rule.accessibilityLabel = [NSString stringWithFormat:@"Rule for %@", d];
    NSStackView* row =
        [NSStackView stackViewWithViews:@[ name, numbers, rule ]];
    [name setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                   forOrientation:
                                       NSLayoutConstraintOrientationHorizontal];
    [row.widthAnchor constraintEqualToConstant:kWidth - 16].active = YES;
    [rows addArrangedSubview:row];
  }
  if (sorted.empty()) {
    [rows addArrangedSubview:Small(@"No third-party requests on this page.")];
  }
  NSScrollView* scroll = [[NSScrollView alloc] init];
  scroll.hasVerticalScroller = YES;
  scroll.drawsBackground = NO;
  NSView* document = [[NSView alloc] init];
  document.translatesAutoresizingMaskIntoConstraints = NO;
  [document addSubview:rows];
  rows.translatesAutoresizingMaskIntoConstraints = NO;
  [NSLayoutConstraint activateConstraints:@[
    [rows.topAnchor constraintEqualToAnchor:document.topAnchor],
    [rows.leadingAnchor constraintEqualToAnchor:document.leadingAnchor],
    [rows.trailingAnchor constraintEqualToAnchor:document.trailingAnchor],
    [rows.bottomAnchor constraintEqualToAnchor:document.bottomAnchor],
  ]];
  scroll.documentView = document;
  [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor]
      .active = YES;
  CGFloat height = std::min<CGFloat>(
      200, static_cast<CGFloat>(std::max<std::size_t>(sorted.size(), 1)) * 26);
  [scroll.heightAnchor constraintEqualToConstant:height].active = YES;
  [scroll.widthAnchor constraintEqualToConstant:kWidth].active = YES;
  NSTextField* domainsTitle = [NSTextField
      labelWithString:[NSString stringWithFormat:@"Third-party domains (%zu)",
                                                 sorted.size()]];
  domainsTitle.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize];

  // Links.
  NSButton* pick = [NSButton buttonWithTitle:@"Block element…"
                                      target:self
                                      action:@selector(pick:)];
  pick.enabled = toggle.state == NSControlStateValueOn && toggle.enabled;
  NSButton* list = [NSButton buttonWithTitle:@"Show blocked requests"
                                      target:self
                                      action:@selector(showRequests:)];
  NSButton* report = [NSButton buttonWithTitle:@"Report a broken site"
                                        target:self
                                        action:@selector(report:)];
  NSButton* settings = [NSButton buttonWithTitle:@"Blocker settings"
                                          target:self
                                          action:@selector(settings:)];
  for (NSButton* button in @[ pick, list, report, settings ]) {
    button.bezelStyle = NSBezelStyleInline;
    button.controlSize = NSControlSizeSmall;
  }
  NSStackView* links = [NSStackView stackViewWithViews:@[ pick, list ]];
  links.spacing = 6;
  NSStackView* more = [NSStackView stackViewWithViews:@[ report, settings ]];
  more.spacing = 6;

  NSStackView* stack = [NSStackView stackViewWithViews:@[
    header, state, counts, domainsTitle, scroll, links, more
  ]];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 8;
  stack.edgeInsets = NSEdgeInsetsMake(14, 14, 14, 14);
  [stack setCustomSpacing:14 afterView:counts];
  self.view = stack;
}

- (void)toggled:(NSSwitch*)sender {
  [_actions setAdblockSiteEnabled:sender.state == NSControlStateValueOn];
  [self.popover close];
}

// Domain rules (007), written to My filters in the lists' syntax.
- (void)addRule:(NSMenuItem*)sender {
  NSArray<NSString*>* info = sender.representedObject;
  id<BRAdblockPopoverActions> actions = _actions;
  NSString* site = [actions adblockPageHost];
  NSString* kind = info[0];
  NSString* domain = info[1];
  NSString* rule = nil;
  if ([kind isEqualToString:@"block"]) {
    rule = [NSString stringWithFormat:@"||%@^$important", domain];
  } else if ([kind isEqualToString:@"block-site"]) {
    rule =
        [NSString stringWithFormat:@"||%@^$important,domain=%@", domain, site];
  } else if ([kind isEqualToString:@"allow"]) {
    rule = [NSString stringWithFormat:@"@@||%@^", domain];
  } else {
    rule = [NSString stringWithFormat:@"@@||%@^$domain=%@", domain, site];
  }
  [actions addAdblockRule:rule];
  [self.popover close];
}

- (void)showRequests:(id)sender {
  id<BRAdblockPopoverActions> actions = _actions;
  [BRAdblockRequestsWindow showRequests:[actions adblockPageRequests]
                                   host:[actions adblockPageHost]
                                actions:actions];
  [self.popover close];
}

- (void)pick:(id)sender {
  [_actions startElementPicker];
  [self.popover close];
}

- (void)report:(id)sender {
  [_actions reportBrokenSite];
  [self.popover close];
}

- (void)settings:(id)sender {
  [_actions openAdblockSettings];
  [self.popover close];
}

@end

@implementation BRAdblockPopover

+ (void)showRelativeTo:(NSView*)anchor
               actions:(id<BRAdblockPopoverActions>)actions {
  BRAdblockPopoverController* controller =
      [[BRAdblockPopoverController alloc] initWithActions:actions];
  NSPopover* popover = [[NSPopover alloc] init];
  popover.behavior = NSPopoverBehaviorTransient;
  popover.contentViewController = controller;
  controller.popover = popover;
  [popover showRelativeToRect:anchor.bounds
                       ofView:anchor
                preferredEdge:NSRectEdgeMaxY];
}

@end

#pragma mark - Blocked requests window

@interface BRAdblockRequestsController
    : NSWindowController <NSTableViewDataSource, NSTableViewDelegate>
@end

@implementation BRAdblockRequestsController {
  std::vector<brouk::AdblockRequestInfo> _rows;
  NSMutableDictionary<NSString*, NSString*>* _listNames;
  BRAdblockRequestsController* _selfReference;  // Alive while open.
}

- (instancetype)initWithRequests:
                    (const std::vector<brouk::AdblockRequestInfo>&)requests
                            host:(NSString*)host
                         actions:(id<BRAdblockPopoverActions>)actions {
  NSWindow* window = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(0, 0, 900, 420)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  window.title = [NSString stringWithFormat:@"Blocked requests — %@", host];
  window.releasedWhenClosed = NO;
  if ((self = [super initWithWindow:window])) {
    _listNames = [NSMutableDictionary dictionary];
    for (const brouk::AdblockRequestInfo& info : requests) {
      if (info.blocked || info.excepted) {
        _rows.push_back(info);
        NSString* key = ToNSString(info.list_id);
        if (!_listNames[key]) {
          _listNames[key] = [actions adblockListName:info.list_id];
        }
      }
    }
    NSTableView* table = [[NSTableView alloc] init];
    table.usesAlternatingRowBackgroundColors = YES;
    table.allowsColumnResizing = YES;
    for (NSArray* column in @[
           @[ @"result", @"Result", @80 ], @[ @"type", @"Type", @90 ],
           @[ @"url", @"Request", @360 ], @[ @"rule", @"Rule", @220 ],
           @[ @"list", @"List", @130 ]
         ]) {
      NSTableColumn* tableColumn =
          [[NSTableColumn alloc] initWithIdentifier:column[0]];
      tableColumn.title = column[1];
      tableColumn.width = [column[2] doubleValue];
      [table addTableColumn:tableColumn];
    }
    table.dataSource = self;
    table.delegate = self;
    NSScrollView* scroll = [[NSScrollView alloc] init];
    scroll.documentView = table;
    scroll.hasVerticalScroller = YES;
    scroll.hasHorizontalScroller = YES;
    window.contentView = scroll;
    [window center];
  }
  return self;
}

- (void)open {
  _selfReference = self;
  [self showWindow:nil];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(windowClosed:)
             name:NSWindowWillCloseNotification
           object:self.window];
}

- (void)windowClosed:(NSNotification*)notification {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  _selfReference = nil;
}

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
  return static_cast<NSInteger>(_rows.size());
}

- (NSView*)tableView:(NSTableView*)tableView
    viewForTableColumn:(NSTableColumn*)column
                   row:(NSInteger)row {
  const brouk::AdblockRequestInfo& info = _rows[static_cast<std::size_t>(row)];
  NSString* identifier = column.identifier;
  NSString* text = @"";
  if ([identifier isEqualToString:@"result"]) {
    text = info.blocked ? @"Blocked" : @"Allowed";
  } else if ([identifier isEqualToString:@"type"]) {
    text = ToNSString(info.type);
  } else if ([identifier isEqualToString:@"url"]) {
    text = ToNSString(info.url);
  } else if ([identifier isEqualToString:@"rule"]) {
    text = ToNSString(info.rule);
  } else {
    text = _listNames[ToNSString(info.list_id)] ?: @"";
  }
  NSTextField* cell = [NSTextField labelWithString:text];
  cell.lineBreakMode = NSLineBreakByTruncatingMiddle;
  cell.selectable = YES;
  cell.toolTip = text;
  if ([identifier isEqualToString:@"result"]) {
    cell.textColor =
        info.blocked ? NSColor.systemRedColor : NSColor.systemGreenColor;
  }
  return cell;
}

@end

@implementation BRAdblockRequestsWindow

+ (void)showRequests:(const std::vector<brouk::AdblockRequestInfo>&)requests
                host:(NSString*)host
             actions:(id<BRAdblockPopoverActions>)actions {
  BRAdblockRequestsController* controller =
      [[BRAdblockRequestsController alloc] initWithRequests:requests
                                                       host:host
                                                    actions:actions];
  [controller open];
}

@end
