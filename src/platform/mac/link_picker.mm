#import "platform/mac/link_picker.h"

#include <algorithm>
#include <string>

#import "platform/mac/help_button.h"
#import "platform/mac/profile_colors.h"

namespace {

constexpr CGFloat kWidth = 520;
constexpr CGFloat kInset = 16;
constexpr CGFloat kProfileRowHeight = 36;
constexpr CGFloat kWindowRowHeight = 88;
constexpr CGFloat kThumbnailWidth = 120;
constexpr CGFloat kThumbnailHeight = 75;
constexpr NSUInteger kVisibleProfileRows = 9;
constexpr NSUInteger kVisibleWindowRows = 5;

// Key codes of keys without characters of their own.
constexpr unsigned short kKeyReturn = 36;
constexpr unsigned short kKeyEnter = 76;
constexpr unsigned short kKeyEscape = 53;
constexpr unsigned short kKeyLeft = 123;

NSTextField* Label(NSString* text, CGFloat size, NSColor* color) {
  NSTextField* label = [NSTextField labelWithString:text];
  label.font = [NSFont systemFontOfSize:size];
  label.textColor = color;
  label.lineBreakMode = NSLineBreakByTruncatingTail;
  return label;
}

// "1"…"9" for the first nine rows, nothing after.
NSString* NumberKey(NSInteger row) {
  return row < 9
             ? [NSString stringWithFormat:@"%ld", static_cast<long>(row + 1)]
             : @"";
}

// The URL with its host in bold: what is about to open, at a glance.
NSAttributedString* UrlText(NSString* url) {
  NSFont* font = [NSFont systemFontOfSize:13];
  NSMutableAttributedString* text = [[NSMutableAttributedString alloc]
      initWithString:url
          attributes:@{
            NSFontAttributeName : font,
            NSForegroundColorAttributeName : NSColor.secondaryLabelColor
          }];
  NSURLComponents* components = [NSURLComponents componentsWithString:url];
  NSRange host = components.host.length ? components.rangeOfHost
                                        : NSMakeRange(NSNotFound, 0);
  if (host.location != NSNotFound) {
    [text addAttributes:@{
      NSFontAttributeName : [NSFont boldSystemFontOfSize:13],
      NSForegroundColorAttributeName : NSColor.labelColor
    }
                  range:host];
  }
  return text;
}

BRLinkPickerResult CancelledResult() {
  BRLinkPickerResult result;
  result.cancelled = true;
  return result;
}

}  // namespace

@implementation BRLinkPickerProfile
@end

@implementation BRLinkPickerGroup
@end

@implementation BRLinkPickerWindow
@end

// Takes the keyboard although it has no title bar.
@interface BRLinkPickerPanel : NSPanel
@end

@implementation BRLinkPickerPanel
- (BOOL)canBecomeKeyWindow {
  return YES;
}
@end

@protocol BRLinkPickerKeys
- (void)chooseRow:(NSInteger)row;
- (void)chooseSelectedRow;
- (void)cancelOrBack;
- (void)back;
- (void)toggleAlways;
@end

// The list: number keys, Return and Escape go to the picker; arrows move
// the selection as in any table.
@interface BRLinkPickerTable : NSTableView
@property(nonatomic, weak) id<BRLinkPickerKeys> keys;
@end

@implementation BRLinkPickerTable

- (void)keyDown:(NSEvent*)event {
  id<BRLinkPickerKeys> keys = self.keys;
  NSString* characters = event.charactersIgnoringModifiers;
  if (characters.length == 1) {
    unichar c = [characters characterAtIndex:0];
    if (c >= '1' && c <= '9') {
      [keys chooseRow:c - '1'];
      return;
    }
    if (c == 'a' || c == 'A') {
      [keys toggleAlways];
      return;
    }
  }
  switch (event.keyCode) {
    case kKeyReturn:
    case kKeyEnter:
      [keys chooseSelectedRow];
      return;
    case kKeyEscape:
      [keys cancelOrBack];
      return;
    case kKeyLeft:
      [keys back];
      return;
    default:
      [super keyDown:event];
  }
}

@end

// A profile row: number key, colour dot, name.
@interface BRLinkPickerProfileRow : NSTableCellView
- (void)showProfile:(BRLinkPickerProfile*)profile row:(NSInteger)row;
@end

@implementation BRLinkPickerProfileRow {
  NSTextField* _key;
  NSImageView* _dot;
  NSTextField* _name;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _key = Label(@"", 12, NSColor.secondaryLabelColor);
    _key.alignment = NSTextAlignmentCenter;
    _dot = [NSImageView imageViewWithImage:[NSImage new]];
    _name = Label(@"", 14, NSColor.labelColor);
    for (NSView* view in @[ _key, _dot, _name ]) {
      [self addSubview:view];
    }
  }
  return self;
}

- (void)showProfile:(BRLinkPickerProfile*)profile row:(NSInteger)row {
  _key.stringValue = NumberKey(row);
  _dot.image = brouk::mac::ProfileDot(profile.color);
  _name.stringValue = profile.name;
  self.accessibilityLabel = profile.name;
  self.needsLayout = YES;
}

- (void)layout {
  [super layout];
  NSRect b = self.bounds;
  _key.frame = NSMakeRect(4, (b.size.height - 16) / 2, 20, 16);
  _dot.frame = NSMakeRect(30, (b.size.height - 12) / 2, 12, 12);
  _name.frame = NSMakeRect(52, (b.size.height - 18) / 2, b.size.width - 60, 18);
}

@end

// A window row: number key, thumbnail of the active tab, its favicon and
// title, and the window's groups as chips.
@interface BRLinkPickerWindowRow : NSTableCellView
- (void)showWindow:(nullable BRLinkPickerWindow*)window
            chosen:(const brouk::ProfileId&)chosen
               row:(NSInteger)row;
@end

@implementation BRLinkPickerWindowRow {
  BRLinkPickerWindow* _shown;
  brouk::ProfileId _chosen;
  NSInteger _row;
  NSTextField* _key;
  NSImageView* _thumbnail;
  NSImageView* _favicon;
  NSTextField* _title;
  NSTextField* _groups;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _key = Label(@"", 12, NSColor.secondaryLabelColor);
    _key.alignment = NSTextAlignmentCenter;
    _thumbnail = [NSImageView imageViewWithImage:[NSImage new]];
    _thumbnail.imageScaling = NSImageScaleProportionallyUpOrDown;
    _thumbnail.wantsLayer = YES;
    _thumbnail.layer.cornerRadius = 4;
    _thumbnail.layer.masksToBounds = YES;
    _thumbnail.layer.borderWidth = 1;
    _favicon = [NSImageView imageViewWithImage:[NSImage new]];
    _favicon.imageScaling = NSImageScaleProportionallyUpOrDown;
    _title = Label(@"", 13, NSColor.labelColor);
    _groups = Label(@"", 12, NSColor.secondaryLabelColor);
    for (NSView* view in @[ _key, _thumbnail, _favicon, _title, _groups ]) {
      [self addSubview:view];
    }
  }
  return self;
}

// On the selected (blue) row the chips need lighter colours to stay
// readable.
- (void)setBackgroundStyle:(NSBackgroundStyle)backgroundStyle {
  [super setBackgroundStyle:backgroundStyle];
  [self showWindow:_shown chosen:_chosen row:_row];
}

- (void)showWindow:(nullable BRLinkPickerWindow*)window
            chosen:(const brouk::ProfileId&)chosen
               row:(NSInteger)row {
  _shown = window;
  _chosen = chosen;
  _row = row;
  BOOL selected = self.backgroundStyle == NSBackgroundStyleEmphasized;
  NSColor* strong =
      selected ? NSColor.alternateSelectedControlTextColor : NSColor.labelColor;
  NSColor* weak = selected ? [NSColor.alternateSelectedControlTextColor
                                 colorWithAlphaComponent:0.75]
                           : NSColor.secondaryLabelColor;
  NSColor* faint = selected ? [NSColor.alternateSelectedControlTextColor
                                  colorWithAlphaComponent:0.5]
                            : NSColor.tertiaryLabelColor;
  _key.stringValue = NumberKey(row);
  _thumbnail.layer.borderColor = NSColor.separatorColor.CGColor;
  if (!window) {
    NSImage* plus = [NSImage imageWithSystemSymbolName:@"macwindow.badge.plus"
                              accessibilityDescription:nil];
    _thumbnail.image = plus;
    _thumbnail.imageScaling = NSImageScaleNone;
    _favicon.image = nil;
    _title.stringValue = @"New window";
    _groups.stringValue = @"";
    self.accessibilityLabel = @"New window";
    self.needsLayout = YES;
    return;
  }
  BOOL hasThumbnail = window.thumbnail != nil;
  _thumbnail.image = window.thumbnail ?: window.favicon;
  _thumbnail.imageScaling =
      hasThumbnail ? NSImageScaleProportionallyUpOrDown : NSImageScaleNone;
  _favicon.image = window.favicon;
  _title.stringValue = window.title.length ? window.title : @"New Tab";

  // "● Work 5 · ● Main 2", the chosen profile's group in bold.
  NSMutableAttributedString* chips = [[NSMutableAttributedString alloc] init];
  NSMutableArray<NSString*>* spoken = [NSMutableArray array];
  for (BRLinkPickerGroup* group in window.groups) {
    if (chips.length) {
      [chips appendAttributedString:[[NSAttributedString alloc]
                                        initWithString:@"  ·  "
                                            attributes:@{
                                              NSForegroundColorAttributeName :
                                                  faint
                                            }]];
    }
    BOOL isChosen = group.profileId == chosen;
    NSDictionary* dot = @{
      NSForegroundColorAttributeName : brouk::mac::ProfileColor(group.color)
    };
    NSDictionary* text = @{
      NSFontAttributeName : isChosen ? [NSFont boldSystemFontOfSize:12]
                                     : [NSFont systemFontOfSize:12],
      NSForegroundColorAttributeName : isChosen ? strong : weak
    };
    [chips
        appendAttributedString:[[NSAttributedString alloc] initWithString:@"● "
                                                               attributes:dot]];
    NSString* label =
        [NSString stringWithFormat:@"%@ %lu", group.name,
                                   static_cast<unsigned long>(group.tabCount)];
    [chips appendAttributedString:[[NSAttributedString alloc]
                                      initWithString:label
                                          attributes:text]];
    [spoken addObject:[NSString stringWithFormat:@"%@, %lu tabs", group.name,
                                                 static_cast<unsigned long>(
                                                     group.tabCount)]];
  }
  _groups.attributedStringValue = chips;
  self.accessibilityLabel =
      [NSString stringWithFormat:@"%@; %@", _title.stringValue,
                                 [spoken componentsJoinedByString:@", "]];
  self.needsLayout = YES;
}

- (void)layout {
  [super layout];
  NSRect b = self.bounds;
  _key.frame = NSMakeRect(4, (b.size.height - 16) / 2, 20, 16);
  _thumbnail.frame = NSMakeRect(30, (b.size.height - kThumbnailHeight) / 2,
                                kThumbnailWidth, kThumbnailHeight);
  CGFloat x = NSMaxX(_thumbnail.frame) + 12;
  CGFloat width = b.size.width - x - 8;
  CGFloat middle = b.size.height / 2;
  BOOL hasFavicon = _favicon.image != nil;
  _favicon.frame = NSMakeRect(x, middle + 3, 16, 16);
  _favicon.hidden = !hasFavicon;
  CGFloat titleX = hasFavicon ? x + 22 : x;
  _title.frame = NSMakeRect(titleX, middle + 2, width - (titleX - x), 18);
  _groups.frame = NSMakeRect(x, middle - 20, width, 16);
}

@end

@interface BRLinkPicker () <NSTableViewDataSource,
                            NSTableViewDelegate,
                            BRLinkPickerKeys>
@end

@implementation BRLinkPicker {
  BRLinkPickerPanel* _panel;
  NSTextField* _url;
  NSTextField* _source;
  NSTextField* _step;
  NSScrollView* _scroll;
  NSLayoutConstraint* _scrollHeight;
  BRLinkPickerTable* _table;
  NSButton* _always;
  NSTextField* _hint;
  NSButton* _backButton;

  NSArray<BRLinkPickerProfile*>* _profiles;
  NSString* _rulePattern;
  NSArray<BRLinkPickerWindow*>* _windows;
  NSArray<BRLinkPickerWindow*>* _Nullable (^_windowsForProfile)(
      const brouk::ProfileId& profile);
  void (^_done)(const BRLinkPickerResult& result);
  BOOL _windowStep;
  brouk::ProfileId _chosenProfile;
}

- (BOOL)isVisible {
  return _panel.isVisible;
}

- (void)buildPanel {
  _panel = [[BRLinkPickerPanel alloc]
      initWithContentRect:NSMakeRect(0, 0, kWidth, 200)
                styleMask:NSWindowStyleMaskBorderless
                  backing:NSBackingStoreBuffered
                    defer:NO];
  _panel.opaque = NO;
  _panel.backgroundColor = NSColor.clearColor;
  _panel.hasShadow = YES;
  _panel.releasedWhenClosed = NO;
  _panel.hidesOnDeactivate = NO;
  _panel.level = NSModalPanelWindowLevel;
  // Shown on the Space the user is on, also over full-screen windows.
  _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                              NSWindowCollectionBehaviorFullScreenAuxiliary;
  _panel.accessibilityLabel = @"Open link";

  NSVisualEffectView* background =
      [[NSVisualEffectView alloc] initWithFrame:_panel.contentView.bounds];
  background.material = NSVisualEffectMaterialPopover;
  background.state = NSVisualEffectStateActive;
  background.wantsLayer = YES;
  background.layer.cornerRadius = 12;
  background.layer.masksToBounds = YES;
  _panel.contentView = background;

  _url = Label(@"", 13, NSColor.labelColor);
  _url.lineBreakMode = NSLineBreakByTruncatingMiddle;
  _url.selectable = YES;
  _source = Label(@"", 12, NSColor.secondaryLabelColor);
  _step = Label(@"", 15, NSColor.labelColor);
  _step.font = [NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];

  _table = [[BRLinkPickerTable alloc] initWithFrame:NSZeroRect];
  NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"entry"];
  column.resizingMask = NSTableColumnAutoresizingMask;
  [_table addTableColumn:column];
  _table.headerView = nil;
  _table.intercellSpacing = NSMakeSize(0, 2);
  _table.backgroundColor = NSColor.clearColor;
  _table.style = NSTableViewStyleInset;
  _table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
  _table.dataSource = self;
  _table.delegate = self;
  _table.target = self;
  _table.action = @selector(rowClicked:);
  _table.keys = self;

  _scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  _scroll.documentView = _table;
  _scroll.drawsBackground = NO;
  _scroll.hasVerticalScroller = YES;
  _scroll.autohidesScrollers = YES;
  _scroll.translatesAutoresizingMaskIntoConstraints = NO;
  _scrollHeight = [_scroll.heightAnchor constraintEqualToConstant:100];
  _scrollHeight.active = YES;

  _always = [NSButton checkboxWithTitle:@"" target:nil action:nil];
  _always.lineBreakMode = NSLineBreakByTruncatingMiddle;

  _hint = Label(@"", 11, NSColor.tertiaryLabelColor);
  _backButton = [NSButton buttonWithTitle:@"Back"
                                   target:self
                                   action:@selector(back)];
  _backButton.bezelStyle = NSBezelStylePush;
  NSButton* cancel = [NSButton buttonWithTitle:@"Cancel"
                                        target:self
                                        action:@selector(cancel)];
  cancel.bezelStyle = NSBezelStylePush;
  NSStackView* footer = [NSStackView stackViewWithViews:@[
    BRHelpButton(@"link-routing-picker"), _hint, _backButton, cancel
  ]];
  footer.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  [_hint setContentHuggingPriority:NSLayoutPriorityDefaultLow
                    forOrientation:NSLayoutConstraintOrientationHorizontal];
  [_hint setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                  forOrientation:
                                      NSLayoutConstraintOrientationHorizontal];

  NSStackView* stack = [NSStackView
      stackViewWithViews:@[ _url, _source, _step, _scroll, _always, footer ]];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 8;
  [stack setCustomSpacing:2 afterView:_url];
  [stack setCustomSpacing:14 afterView:_source];
  stack.edgeInsets = NSEdgeInsetsMake(kInset, kInset, kInset, kInset);
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  [background addSubview:stack];
  for (NSView* view in @[ _url, _source, _scroll, _always, footer ]) {
    [view.widthAnchor constraintEqualToAnchor:stack.widthAnchor
                                     constant:-2 * kInset]
        .active = YES;
  }
  [NSLayoutConstraint activateConstraints:@[
    [stack.leadingAnchor constraintEqualToAnchor:background.leadingAnchor],
    [stack.trailingAnchor constraintEqualToAnchor:background.trailingAnchor],
    [stack.topAnchor constraintEqualToAnchor:background.topAnchor],
    [stack.bottomAnchor constraintEqualToAnchor:background.bottomAnchor],
    [stack.widthAnchor constraintEqualToConstant:kWidth],
  ]];
}

- (void)showURL:(NSString*)url
                  via:(nullable NSString*)wrapper
                 from:(NSString*)source
             profiles:(NSArray<BRLinkPickerProfile*>*)profiles
          rulePattern:(nullable NSString*)rulePattern
    windowsForProfile:(NSArray<BRLinkPickerWindow*>* _Nullable (^)(
                          const brouk::ProfileId& profile))windowsForProfile
             onScreen:(nullable NSScreen*)screen
                 done:(void (^)(const BRLinkPickerResult& result))done {
  if (!_panel) {
    [self buildPanel];
  }
  if (_done) {
    [self finish:CancelledResult()];
  }
  _profiles = [profiles copy];
  _rulePattern = [rulePattern copy];
  _windowsForProfile = [windowsForProfile copy];
  _done = [done copy];

  _url.attributedStringValue = UrlText(url);
  _url.toolTip = url;
  NSString* from = wrapper.length ? [NSString stringWithFormat:@"%@ · via %@",
                                                               source, wrapper]
                                  : source;
  _source.stringValue = from;
  _source.hidden = from.length == 0;

  if (_profiles.count > 1) {
    [self showProfileStep];
  } else if (_profiles.count == 1) {
    _chosenProfile = _profiles.firstObject.profileId;
    if (![self showWindowStep]) {
      [self finishWithProfile:_chosenProfile window:nil newWindow:NO];
      return;
    }
  } else {
    [self finish:CancelledResult()];
    return;
  }

  NSScreen* target = screen ?: NSScreen.mainScreen;
  [self layoutPanel];
  NSRect area = target.visibleFrame;
  NSRect frame = _panel.frame;
  frame.origin =
      NSMakePoint(NSMidX(area) - frame.size.width / 2,
                  NSMidY(area) - frame.size.height / 2 + area.size.height / 10);
  [_panel setFrame:frame display:NO];
  [_panel makeKeyAndOrderFront:nil];
  [_panel makeFirstResponder:_table];
}

- (void)setThumbnail:(NSImage*)thumbnail forWindow:(std::int64_t)windowId {
  for (NSUInteger i = 0; i < _windows.count; ++i) {
    if (_windows[i].windowId == windowId) {
      _windows[i].thumbnail = thumbnail;
      if (_windowStep && _panel.isVisible) {
        [_table reloadDataForRowIndexes:[NSIndexSet indexSetWithIndex:i]
                          columnIndexes:[NSIndexSet indexSetWithIndex:0]];
      }
    }
  }
}

#pragma mark - Steps

- (void)showProfileStep {
  _windowStep = NO;
  _step.stringValue = @"Open in which profile?";
  _table.rowHeight = kProfileRowHeight;
  _table.accessibilityLabel = @"Profiles";
  _always.hidden = _rulePattern.length == 0;
  _always.title = [NSString
      stringWithFormat:@"Always open links matching %@ in this profile (A)",
                       _rulePattern ?: @""];
  _always.state = NSControlStateValueOff;
  _backButton.hidden = YES;
  _hint.stringValue = @"1–9 or ↑↓ and Return to choose · Esc to cancel";
  [self reloadSelectingRow:0];
}

// NO if the chosen profile needs no window step.
- (BOOL)showWindowStep {
  NSArray<BRLinkPickerWindow*>* windows =
      _windowsForProfile ? _windowsForProfile(_chosenProfile) : nil;
  if (windows.count == 0) {
    return NO;
  }
  _windows = windows;
  _windowStep = YES;
  NSString* name = @"";
  for (BRLinkPickerProfile* profile in _profiles) {
    if (profile.profileId == _chosenProfile) {
      name = profile.name;
    }
  }
  _step.stringValue =
      name.length
          ? [NSString stringWithFormat:@"Open in which %@ window?", name]
          : @"Open in which window?";
  _table.rowHeight = kWindowRowHeight;
  _table.accessibilityLabel = @"Windows";
  _always.hidden = YES;
  _backButton.hidden = _profiles.count <= 1;
  _hint.stringValue =
      _profiles.count > 1
          ? @"1–9 or ↑↓ and Return to choose · Esc or ← to go back"
          : @"1–9 or ↑↓ and Return to choose · Esc to cancel";
  [self reloadSelectingRow:0];
  return YES;
}

- (void)reloadSelectingRow:(NSInteger)row {
  [_table reloadData];
  if ([self rowCount] > 0) {
    [_table selectRowIndexes:[NSIndexSet indexSetWithIndex:row]
        byExtendingSelection:NO];
    [_table scrollRowToVisible:row];
  }
  if (_panel.isVisible) {
    [self layoutPanel];
  }
}

// The list shows up to a few rows without scrolling; the panel grows
// downwards from its top edge.
- (void)layoutPanel {
  NSUInteger visible = _windowStep ? kVisibleWindowRows : kVisibleProfileRows;
  NSUInteger rows = std::min<NSUInteger>([self rowCount], visible);
  _scrollHeight.constant =
      rows * (_table.rowHeight + _table.intercellSpacing.height) + 12;
  NSSize size = _panel.contentView.fittingSize;
  NSRect frame = _panel.frame;
  CGFloat top = NSMaxY(frame);
  frame.size = size;
  frame.origin.y = _panel.isVisible ? top - size.height : frame.origin.y;
  [_panel setFrame:frame display:YES];
}

- (NSInteger)rowCount {
  return _windowStep ? static_cast<NSInteger>(_windows.count) + 1
                     : static_cast<NSInteger>(_profiles.count);
}

#pragma mark - Choosing

- (void)rowClicked:(id)sender {
  if (_table.clickedRow >= 0) {
    [self chooseRow:_table.clickedRow];
  }
}

- (void)chooseSelectedRow {
  if (_table.selectedRow >= 0) {
    [self chooseRow:_table.selectedRow];
  }
}

- (void)chooseRow:(NSInteger)row {
  if (row < 0 || row >= [self rowCount]) {
    return;
  }
  if (!_windowStep) {
    _chosenProfile = _profiles[static_cast<NSUInteger>(row)].profileId;
    if (![self showWindowStep]) {
      [self finishWithProfile:_chosenProfile window:nil newWindow:NO];
    }
    return;
  }
  if (row == static_cast<NSInteger>(_windows.count)) {
    [self finishWithProfile:_chosenProfile window:nil newWindow:YES];
  } else {
    [self finishWithProfile:_chosenProfile
                     window:_windows[static_cast<NSUInteger>(row)]
                  newWindow:NO];
  }
}

- (void)cancelOrBack {
  if (_windowStep && _profiles.count > 1) {
    [self back];
  } else {
    [self cancel];
  }
}

- (void)back {
  if (!_windowStep || _profiles.count <= 1) {
    return;
  }
  NSInteger row = 0;
  for (NSUInteger i = 0; i < _profiles.count; ++i) {
    if (_profiles[i].profileId == _chosenProfile) {
      row = static_cast<NSInteger>(i);
    }
  }
  BOOL always = _always.state == NSControlStateValueOn;
  [self showProfileStep];
  _always.state = always ? NSControlStateValueOn : NSControlStateValueOff;
  [_table selectRowIndexes:[NSIndexSet indexSetWithIndex:row]
      byExtendingSelection:NO];
}

- (void)cancel {
  [self finish:CancelledResult()];
}

// "A": the checkbox without the mouse (Tab reaches it only with macOS's
// full keyboard navigation on).
- (void)toggleAlways {
  if (_windowStep || _always.hidden) {
    return;
  }
  _always.state = _always.state == NSControlStateValueOn
                      ? NSControlStateValueOff
                      : NSControlStateValueOn;
}

- (void)finishWithProfile:(const brouk::ProfileId&)profile
                   window:(nullable BRLinkPickerWindow*)window
                newWindow:(BOOL)newWindow {
  BRLinkPickerResult result;
  result.profile = profile;
  result.always = _profiles.count > 1 && _rulePattern.length > 0 &&
                  _always.state == NSControlStateValueOn;
  if (window) {
    result.window = BRLinkPickerResult::Window::kExisting;
    result.window_id = window.windowId;
  } else if (newWindow) {
    result.window = BRLinkPickerResult::Window::kNewWindow;
  }
  [self finish:result];
}

- (void)finish:(const BRLinkPickerResult&)result {
  [_panel orderOut:nil];
  void (^done)(const BRLinkPickerResult&) = _done;
  _done = nil;
  _windowsForProfile = nil;
  _windows = nil;
  if (done) {
    done(result);
  }
}

#pragma mark - NSTableView

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
  return [self rowCount];
}

- (nullable NSView*)tableView:(NSTableView*)tableView
           viewForTableColumn:(nullable NSTableColumn*)tableColumn
                          row:(NSInteger)row {
  if (!_windowStep) {
    BRLinkPickerProfileRow* view = [tableView makeViewWithIdentifier:@"profile"
                                                               owner:self];
    if (!view) {
      view = [[BRLinkPickerProfileRow alloc] initWithFrame:NSZeroRect];
      view.identifier = @"profile";
    }
    [view showProfile:_profiles[static_cast<NSUInteger>(row)] row:row];
    return view;
  }
  BRLinkPickerWindowRow* view = [tableView makeViewWithIdentifier:@"window"
                                                            owner:self];
  if (!view) {
    view = [[BRLinkPickerWindowRow alloc] initWithFrame:NSZeroRect];
    view.identifier = @"window";
  }
  BRLinkPickerWindow* window = row < static_cast<NSInteger>(_windows.count)
                                   ? _windows[static_cast<NSUInteger>(row)]
                                   : nil;
  [view showWindow:window chosen:_chosenProfile row:row];
  return view;
}

@end
