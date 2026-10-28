#import "platform/mac/bookmarks_bar.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

NSPasteboardType const BRBookmarkPasteboardType =
    @"io.github.dreaddcz.brouk.bookmark";

namespace {

constexpr CGFloat kItemHeight = 22;
constexpr CGFloat kMaxItemWidth = 170;
constexpr CGFloat kSpacing = 2;
constexpr CGFloat kSideInset = 8;
constexpr CGFloat kOverflowWidth = 26;
// A drag starts after the mouse moved this far (points).
constexpr CGFloat kDragThreshold = 4;

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

NSString* TitleOf(const brouk::BookmarkItem& item) {
  return item.name.empty() ? ToNSString(item.url) : ToNSString(item.name);
}

// The site's icon from the profile's history (013), else a generic one.
NSImage* IconOf(const brouk::BookmarkItem& item,
                BRFaviconCache* _Nullable favicons,
                const brouk::ProfileId& profile) {
  if (!item.folder) {
    if (NSImage* icon = [favicons iconForURL:item.url profile:profile]) {
      return icon;
    }
  }
  return [NSImage imageWithSystemSymbolName:item.folder ? @"folder" : @"globe"
                   accessibilityDescription:nil];
}

BRBookmarkOpenDisposition DispositionFor(NSEventModifierFlags flags,
                                         BOOL middle) {
  BOOL command = (flags & NSEventModifierFlagCommand) != 0;
  BOOL shift = (flags & NSEventModifierFlagShift) != 0;
  if (command && shift) {
    return BRBookmarkOpenForegroundTab;
  }
  if (command || middle) {
    return BRBookmarkOpenBackgroundTab;
  }
  if (shift) {
    return BRBookmarkOpenNewWindow;  // As in Chrome.
  }
  return BRBookmarkOpenCurrentTab;
}

// Waits for the mouse to be released or dragged after `down`: true when it
// was dragged far enough to start a drag.
BOOL TrackDrag(NSWindow* window, NSEvent* down) {
  NSPoint start = down.locationInWindow;
  while (true) {
    NSEvent* next = [window nextEventMatchingMask:NSEventMaskLeftMouseUp |
                                                  NSEventMaskLeftMouseDragged];
    if (next.type == NSEventTypeLeftMouseUp) {
      return NO;
    }
    NSPoint at = next.locationInWindow;
    if (std::hypot(at.x - start.x, at.y - start.y) > kDragThreshold) {
      return YES;
    }
  }
}

NSImage* SnapshotOf(NSView* view) {
  NSBitmapImageRep* rep =
      [view bitmapImageRepForCachingDisplayInRect:view.bounds];
  [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
  NSImage* image = [[NSImage alloc] initWithSize:view.bounds.size];
  [image addRepresentation:rep];
  return image;
}

}  // namespace

@class BRBookmarksBar;

// One item on the bar.
@interface BRBarItemButton : NSButton
@property(nonatomic) brouk::BookmarkId bookmarkId;
@property(nonatomic) BOOL folder;
@property(nonatomic, weak) BRBookmarksBar* bar;
@end

@interface BRBookmarksBar ()
- (void)itemClicked:(BRBarItemButton*)button
          modifiers:(NSEventModifierFlags)modifiers
             middle:(BOOL)middle;
- (void)beginDragOf:(BRBarItemButton*)button event:(NSEvent*)event;
- (NSMenu*)contextMenuFor:(nullable BRBarItemButton*)button;
@end

@implementation BRBarItemButton

- (void)mouseDown:(NSEvent*)event {
  self.highlighted = YES;
  BOOL dragged = TrackDrag(self.window, event);
  self.highlighted = NO;
  if (dragged) {
    [self.bar beginDragOf:self event:event];
  } else {
    [self.bar itemClicked:self modifiers:event.modifierFlags middle:NO];
  }
}

- (void)otherMouseUp:(NSEvent*)event {
  if (event.buttonNumber == 2 &&
      NSPointInRect([self convertPoint:event.locationInWindow fromView:nil],
                    self.bounds)) {
    [self.bar itemClicked:self modifiers:event.modifierFlags middle:YES];
  }
}

- (void)otherMouseDown:(NSEvent*)event {
  // Handled on release, as a click.
}

- (NSMenu*)menuForEvent:(NSEvent*)event {
  return [self.bar contextMenuFor:self];
}

@end

@implementation BRBookmarksBar {
  __weak BRBookmarkStore* _store;
  brouk::ProfileId _profile;
  std::optional<std::uint64_t> _shownRevision;
  NSMutableArray<BRBarItemButton*>* _buttons;
  // The bar folder's index of each button's item (hidden items, linked to
  // other profiles, sit between them).
  std::vector<std::size_t> _modelIndex;
  NSButton* _overflow;
  NSTextField* _emptyHint;
  // Drag feedback: the insertion line, or the folder a drop goes into.
  std::optional<CGFloat> _dropLineX;
  __weak BRBarItemButton* _dropFolder;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _buttons = [NSMutableArray array];
    _overflow = [NSButton buttonWithTitle:@"»"
                                   target:self
                                   action:@selector(showOverflow:)];
    _overflow.bezelStyle = NSBezelStyleRecessed;
    _overflow.showsBorderOnlyWhileMouseInside = YES;
    _overflow.toolTip = @"More bookmarks";
    _overflow.accessibilityLabel = @"More bookmarks";
    _overflow.hidden = YES;
    [self addSubview:_overflow];
    _emptyHint = [NSTextField
        labelWithString:@"For quick access, place your bookmarks here on "
                        @"the bookmarks bar."];
    _emptyHint.font = [NSFont systemFontOfSize:11];
    _emptyHint.textColor = NSColor.secondaryLabelColor;
    _emptyHint.hidden = YES;
    [self addSubview:_emptyHint];
    [self registerForDraggedTypes:@[
      BRBookmarkPasteboardType, NSPasteboardTypeURL, NSPasteboardTypeString
    ]];
    self.accessibilityRole = NSAccessibilityToolbarRole;
    self.accessibilityLabel = @"Bookmarks bar";
    [NSNotificationCenter.defaultCenter
        addObserver:self
           selector:@selector(faviconsChanged:)
               name:BRFaviconsDidChangeNotification
             object:nil];
  }
  return self;
}

- (void)faviconsChanged:(NSNotification*)notification {
  if (notification.object == _favicons && _store) {
    [self rebuild];
  }
}

- (BOOL)isFlipped {
  return NO;
}

- (void)drawRect:(NSRect)dirtyRect {
  [NSColor.windowBackgroundColor setFill];
  NSRectFill(self.bounds);
  [NSColor.separatorColor setFill];
  NSRectFill(NSMakeRect(0, 0, self.bounds.size.width, 1));
  if (_dropLineX) {
    [NSColor.controlAccentColor setFill];
    NSRectFill(NSMakeRect(*_dropLineX - 1, 4, 2, self.bounds.size.height - 8));
  }
}

#pragma mark - Contents

- (void)showStore:(BRBookmarkStore*)store
          profile:(const brouk::ProfileId&)profile {
  if (store == _store && profile == _profile &&
      _shownRevision == [store model].revision()) {
    return;
  }
  _store = store;
  _profile = profile;
  _shownRevision = [store model].revision();
  [self rebuild];
}

- (void)rebuild {
  for (NSButton* button in _buttons) {
    [button removeFromSuperview];
  }
  [_buttons removeAllObjects];
  _modelIndex.clear();
  BRBookmarkStore* store = _store;
  if (!store) {
    return;
  }
  const brouk::BookmarkModel& model = [store model];
  const brouk::BookmarkItem* bar = model.Find(brouk::BookmarkModel::kBarId);
  for (std::size_t i = 0; i < bar->children.size(); ++i) {
    brouk::BookmarkId id = bar->children[i];
    const brouk::BookmarkItem* item = model.Find(id);
    if (item == nullptr || !model.IsVisibleIn(id, _profile)) {
      continue;
    }
    // The action serves VoiceOver and keyboard presses; mouse clicks are
    // handled in mouseDown: (modifiers, dragging).
    BRBarItemButton* button =
        [BRBarItemButton buttonWithTitle:TitleOf(*item)
                                  target:self
                                  action:@selector(itemPressed:)];
    button.bookmarkId = id;
    button.folder = item->folder;
    button.bar = self;
    button.bezelStyle = NSBezelStyleRecessed;
    button.showsBorderOnlyWhileMouseInside = YES;
    button.image = IconOf(*item, _favicons, _profile);
    button.imagePosition = NSImageLeading;
    button.imageScaling = NSImageScaleProportionallyDown;
    button.font = [NSFont systemFontOfSize:12];
    button.lineBreakMode = NSLineBreakByTruncatingTail;
    button.toolTip = item->folder
                         ? TitleOf(*item)
                         : [NSString stringWithFormat:@"%@\n%@", TitleOf(*item),
                                                      ToNSString(item->url)];
    button.accessibilityLabel =
        item->folder ? [@"Folder " stringByAppendingString:TitleOf(*item)]
                     : TitleOf(*item);
    [self addSubview:button];
    [_buttons addObject:button];
    _modelIndex.push_back(i);
  }
  _emptyHint.hidden = _buttons.count > 0;
  self.needsLayout = YES;
}

- (void)layout {
  [super layout];
  NSRect b = self.bounds;
  CGFloat y = std::floor((b.size.height - kItemHeight) / 2) + 1;
  CGFloat x = kSideInset;
  CGFloat limit = b.size.width - kSideInset;
  // Leave room for "»" only when something does not fit.
  CGFloat total = kSideInset;
  for (NSButton* button in _buttons) {
    [button sizeToFit];
    total += std::min(button.frame.size.width, kMaxItemWidth) + kSpacing;
  }
  BOOL overflow = total > limit;
  if (overflow) {
    limit -= kOverflowWidth + kSpacing;
  }
  BOOL full = NO;
  for (NSButton* button in _buttons) {
    CGFloat width = std::min(button.frame.size.width, kMaxItemWidth);
    if (full || x + width > limit) {
      full = YES;
      button.hidden = YES;
      continue;
    }
    button.hidden = NO;
    button.frame = NSMakeRect(x, y, width, kItemHeight);
    x += width + kSpacing;
  }
  _overflow.hidden = !overflow;
  _overflow.frame = NSMakeRect(b.size.width - kSideInset - kOverflowWidth, y,
                               kOverflowWidth, kItemHeight);
  [_emptyHint sizeToFit];
  _emptyHint.frame = NSMakeRect(
      kSideInset + 4, y + 3,
      std::min(_emptyHint.frame.size.width, b.size.width - 2 * kSideInset),
      _emptyHint.frame.size.height);
}

#pragma mark - Opening

- (void)itemClicked:(BRBarItemButton*)button
          modifiers:(NSEventModifierFlags)modifiers
             middle:(BOOL)middle {
  if (button.folder) {
    if (middle) {
      [self.delegate bookmarksBar:self openAllIn:button.bookmarkId];
      return;
    }
    NSMenu* menu = [self menuForFolder:button.bookmarkId];
    [menu popUpMenuPositioningItem:nil
                        atLocation:NSMakePoint(0, -3)
                            inView:button];
    return;
  }
  [self.delegate bookmarksBar:self
                 openBookmark:button.bookmarkId
                  disposition:DispositionFor(modifiers, middle)];
}

- (void)itemPressed:(BRBarItemButton*)button {
  [self itemClicked:button
          modifiers:NSApp.currentEvent.modifierFlags
             middle:NO];
}

// A folder as a menu: bookmarks open on choosing (with the modifiers held
// then), subfolders are submenus.
- (NSMenu*)menuForFolder:(brouk::BookmarkId)folderId {
  NSMenu* menu = [[NSMenu alloc] init];
  BRBookmarkStore* store = _store;
  if (!store) {
    return menu;
  }
  const brouk::BookmarkModel& model = [store model];
  const brouk::BookmarkItem* folder = model.Find(folderId);
  if (folder == nullptr) {
    return menu;
  }
  for (brouk::BookmarkId id : folder->children) {
    const brouk::BookmarkItem* item = model.Find(id);
    if (item == nullptr || !model.IsVisibleIn(id, _profile)) {
      continue;
    }
    NSMenuItem* entry = [menu addItemWithTitle:TitleOf(*item)
                                        action:nil
                                 keyEquivalent:@""];
    entry.image = IconOf(*item, _favicons, _profile);
    entry.tag = static_cast<NSInteger>(id);
    if (item->folder) {
      entry.submenu = [self menuForFolder:id];
    } else {
      entry.target = self;
      entry.action = @selector(openFromMenu:);
      entry.toolTip = ToNSString(item->url);
    }
  }
  if (menu.numberOfItems == 0) {
    [menu addItemWithTitle:@"(Empty)" action:nil keyEquivalent:@""].enabled =
        NO;
  }
  return menu;
}

- (void)openFromMenu:(NSMenuItem*)sender {
  [self.delegate
      bookmarksBar:self
      openBookmark:static_cast<brouk::BookmarkId>(sender.tag)
       disposition:DispositionFor(NSApp.currentEvent.modifierFlags, NO)];
}

- (void)showOverflow:(id)sender {
  NSMenu* menu = [[NSMenu alloc] init];
  BRBookmarkStore* store = _store;
  for (BRBarItemButton* button in _buttons) {
    if (!button.hidden || !store) {
      continue;
    }
    const brouk::BookmarkItem* item = [store model].Find(button.bookmarkId);
    if (item == nullptr) {
      continue;
    }
    NSMenuItem* entry = [menu addItemWithTitle:TitleOf(*item)
                                        action:nil
                                 keyEquivalent:@""];
    entry.image = IconOf(*item, _favicons, _profile);
    entry.tag = static_cast<NSInteger>(button.bookmarkId);
    if (item->folder) {
      entry.submenu = [self menuForFolder:button.bookmarkId];
    } else {
      entry.target = self;
      entry.action = @selector(openFromMenu:);
    }
  }
  [menu popUpMenuPositioningItem:nil
                      atLocation:NSMakePoint(0, -3)
                          inView:_overflow];
}

#pragma mark - Right-click menu

- (NSMenu*)menuForEvent:(NSEvent*)event {
  return [self contextMenuFor:nil];
}

- (NSMenuItem*)addTo:(NSMenu*)menu
               title:(NSString*)title
              action:(SEL)action
                 tag:(brouk::BookmarkId)tag {
  NSMenuItem* item = [menu addItemWithTitle:title
                                     action:action
                              keyEquivalent:@""];
  item.target = self;
  item.tag = static_cast<NSInteger>(tag);
  return item;
}

- (NSMenu*)contextMenuFor:(nullable BRBarItemButton*)button {
  NSMenu* menu = [[NSMenu alloc] init];
  BRBookmarkStore* store = _store;
  if (!store) {
    return menu;
  }
  brouk::BookmarkId id = button ? button.bookmarkId : 0;
  if (button && button.folder) {
    std::size_t count = 0;
    const brouk::BookmarkModel& model = [store model];
    for (brouk::BookmarkId child : model.Find(id)->children) {
      const brouk::BookmarkItem* item = model.Find(child);
      count += item && !item->folder && model.IsVisibleIn(child, _profile);
    }
    NSMenuItem* all =
        [self addTo:menu
              title:[NSString stringWithFormat:@"Open All (%zu)", count]
             action:@selector(openAll:)
                tag:id];
    all.enabled = count > 0;
    [menu addItem:NSMenuItem.separatorItem];
  } else if (button) {
    [self addTo:menu
          title:@"Open in New Tab"
         action:@selector(openInNewTab:)
            tag:id];
    [self addTo:menu
          title:@"Open in New Window"
         action:@selector(openInNewWindow:)
            tag:id];
    [menu addItem:NSMenuItem.separatorItem];
  }
  if (button) {
    [self addTo:menu title:@"Edit…" action:@selector(editItem:) tag:id];
    [self addTo:menu title:@"Delete" action:@selector(deleteItem:) tag:id];
    [menu addItem:NSMenuItem.separatorItem];
  }
  // New items go after the item clicked, or at the end.
  [self addTo:menu
        title:@"Add Bookmark…"
       action:@selector(addBookmark:)
          tag:id];
  [self addTo:menu title:@"Add Folder…" action:@selector(addFolder:) tag:id];
  [menu addItem:NSMenuItem.separatorItem];
  NSMenuItem* manager = [menu addItemWithTitle:@"Bookmark Manager"
                                        action:@selector(showBookmarkManager:)
                                 keyEquivalent:@""];
  manager.target = nil;  // The window's action.
  NSMenuItem* always = [self addTo:menu
                             title:@"Always Show Bookmarks Bar"
                            action:@selector(toggleAlways:)
                               tag:0];
  always.state = [self.delegate bookmarksBarAlwaysShown:self]
                     ? NSControlStateValueOn
                     : NSControlStateValueOff;
  return menu;
}

- (void)openAll:(NSMenuItem*)sender {
  [self.delegate bookmarksBar:self
                    openAllIn:static_cast<brouk::BookmarkId>(sender.tag)];
}

- (void)openInNewTab:(NSMenuItem*)sender {
  [self.delegate bookmarksBar:self
                 openBookmark:static_cast<brouk::BookmarkId>(sender.tag)
                  disposition:BRBookmarkOpenForegroundTab];
}

- (void)openInNewWindow:(NSMenuItem*)sender {
  [self.delegate bookmarksBar:self
                 openBookmark:static_cast<brouk::BookmarkId>(sender.tag)
                  disposition:BRBookmarkOpenNewWindow];
}

- (void)editItem:(NSMenuItem*)sender {
  [self.delegate bookmarksBar:self
                         edit:static_cast<brouk::BookmarkId>(sender.tag)];
}

- (void)deleteItem:(NSMenuItem*)sender {
  [self.delegate bookmarksBar:self
                       remove:static_cast<brouk::BookmarkId>(sender.tag)];
}

// The bar folder's index after item `tag` (or its end for 0).
- (std::size_t)indexAfter:(brouk::BookmarkId)tag {
  BRBookmarkStore* store = _store;
  const std::vector<brouk::BookmarkId>& children =
      [store model].Find(brouk::BookmarkModel::kBarId)->children;
  for (std::size_t i = 0; i < children.size(); ++i) {
    if (children[i] == tag) {
      return i + 1;
    }
  }
  return children.size();
}

- (void)addBookmark:(NSMenuItem*)sender {
  [self.delegate bookmarksBar:self
                addBookmarkIn:brouk::BookmarkModel::kBarId
                        index:[self indexAfter:sender.tag]];
}

- (void)addFolder:(NSMenuItem*)sender {
  [self.delegate bookmarksBar:self
                  addFolderIn:brouk::BookmarkModel::kBarId
                        index:[self indexAfter:sender.tag]];
}

- (void)toggleAlways:(id)sender {
  [self.delegate bookmarksBarToggleAlwaysShow:self];
}

#pragma mark - Dragging out

- (void)beginDragOf:(BRBarItemButton*)button event:(NSEvent*)event {
  BRBookmarkStore* store = _store;
  const brouk::BookmarkItem* item =
      store ? [store model].Find(button.bookmarkId) : nullptr;
  if (item == nullptr) {
    return;
  }
  NSPasteboardItem* data = [[NSPasteboardItem alloc] init];
  [data setString:[NSString stringWithFormat:@"%lld", static_cast<long long>(
                                                          button.bookmarkId)]
          forType:BRBookmarkPasteboardType];
  if (!item->folder) {
    // Elsewhere (another app, a page) it is a link.
    [data setString:ToNSString(item->url) forType:NSPasteboardTypeURL];
    [data setString:ToNSString(item->url) forType:NSPasteboardTypeString];
  }
  NSDraggingItem* dragging =
      [[NSDraggingItem alloc] initWithPasteboardWriter:data];
  [dragging setDraggingFrame:button.bounds contents:SnapshotOf(button)];
  [button beginDraggingSessionWithItems:@[ dragging ] event:event source:self];
}

- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
  return context == NSDraggingContextWithinApplication
             ? NSDragOperationMove | NSDragOperationCopy
             : NSDragOperationCopy | NSDragOperationLink;
}

#pragma mark - Dropping

// Where a drop at `point` goes: into a folder button's middle, or between
// items (an index of the bar folder, and the x of the insertion line).
- (void)dropTargetAt:(NSPoint)point
              folder:(BRBarItemButton* __autoreleasing _Nullable*)folder
               index:(std::size_t*)index
               lineX:(CGFloat*)lineX {
  *folder = nil;
  BRBookmarkStore* store = _store;
  std::size_t end =
      store ? [store model].Find(brouk::BookmarkModel::kBarId)->children.size()
            : 0;
  *index = end;
  *lineX = kSideInset;
  for (NSUInteger i = 0; i < _buttons.count; ++i) {
    BRBarItemButton* button = _buttons[i];
    if (button.hidden) {
      break;
    }
    NSRect frame = button.frame;
    *lineX = NSMaxX(frame) + kSpacing / 2;
    if (point.x > NSMaxX(frame)) {
      continue;
    }
    CGFloat quarter = frame.size.width / 4;
    if (button.folder && point.x > NSMinX(frame) + quarter &&
        point.x < NSMaxX(frame) - quarter) {
      *folder = button;
      return;
    }
    if (point.x < NSMidX(frame) || button.folder) {
      *index = _modelIndex[i];
      *lineX = NSMinX(frame) - kSpacing / 2;
    } else {
      *index = _modelIndex[i] + 1;
    }
    return;
  }
}

- (std::optional<brouk::BookmarkId>)draggedBookmark:(id<NSDraggingInfo>)info {
  NSString* text =
      [info.draggingPasteboard stringForType:BRBookmarkPasteboardType];
  if (!text) {
    return std::nullopt;
  }
  long long id = text.longLongValue;
  BRBookmarkStore* store = _store;
  if (id <= 0 || !store || ![store model].Find(id)) {
    return std::nullopt;
  }
  return static_cast<brouk::BookmarkId>(id);
}

// A dropped link: its URL and title, from a page, the address bar or
// another app.
- (nullable NSArray<NSString*>*)draggedLink:(id<NSDraggingInfo>)info {
  NSPasteboard* pasteboard = info.draggingPasteboard;
  NSArray<NSURL*>* urls = [pasteboard readObjectsForClasses:@[ NSURL.class ]
                                                    options:nil];
  NSString* url = urls.firstObject.absoluteString;
  if (!url) {
    NSString* text = [pasteboard stringForType:NSPasteboardTypeString];
    NSURL* parsed = text ? [NSURL URLWithString:text] : nil;
    url = parsed.scheme ? parsed.absoluteString : nil;
  }
  NSString* scheme =
      url ? [NSURL URLWithString:url].scheme.lowercaseString : nil;
  if (!url ||
      !([scheme isEqualToString:@"http"] || [scheme isEqualToString:@"https"] ||
        [scheme isEqualToString:@"file"])) {
    return nil;
  }
  NSString* title = [pasteboard stringForType:@"public.url-name"];
  return @[ url, title.length ? title : url ];
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)info {
  std::optional<brouk::BookmarkId> bookmark = [self draggedBookmark:info];
  if (!bookmark && ![self draggedLink:info]) {
    [self clearDropFeedback];
    return NSDragOperationNone;
  }
  NSPoint point = [self convertPoint:info.draggingLocation fromView:nil];
  BRBarItemButton* folder = nil;
  std::size_t index = 0;
  CGFloat lineX = 0;
  [self dropTargetAt:point folder:&folder index:&index lineX:&lineX];
  if (folder && bookmark && *bookmark == folder.bookmarkId) {
    folder = nil;
  }
  _dropFolder.highlighted = NO;
  _dropFolder = folder;
  folder.highlighted = YES;
  _dropLineX = folder ? std::nullopt : std::optional<CGFloat>(lineX);
  self.needsDisplay = YES;
  return bookmark ? NSDragOperationMove : NSDragOperationCopy;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info {
  return [self draggingUpdated:info];
}

- (void)clearDropFeedback {
  _dropFolder.highlighted = NO;
  _dropFolder = nil;
  _dropLineX.reset();
  self.needsDisplay = YES;
}

- (void)draggingExited:(nullable id<NSDraggingInfo>)info {
  [self clearDropFeedback];
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
  NSPoint point = [self convertPoint:info.draggingLocation fromView:nil];
  BRBarItemButton* folderButton = nil;
  std::size_t index = 0;
  CGFloat lineX = 0;
  [self dropTargetAt:point folder:&folderButton index:&index lineX:&lineX];
  [self clearDropFeedback];
  brouk::BookmarkId folder =
      folderButton ? folderButton.bookmarkId : brouk::BookmarkModel::kBarId;
  if (folderButton) {
    index = brouk::BookmarkModel::kEnd;
  }
  BRBookmarkStore* store = _store;
  if (std::optional<brouk::BookmarkId> bookmark = [self draggedBookmark:info]) {
    if (*bookmark == folder || !store) {
      return NO;
    }
    bool moved = false;
    [store change:[&](brouk::BookmarkModel& model) {
      moved = !model.Move(*bookmark, folder, index);
    }];
    return moved;
  }
  NSArray<NSString*>* link = [self draggedLink:info];
  if (!link) {
    return NO;
  }
  [self.delegate bookmarksBar:self
                      dropURL:link[0]
                        title:link[1]
                       folder:folder
                        index:index];
  return YES;
}

@end

@implementation BRLinkDragButton

- (void)mouseDown:(NSEvent*)event {
  NSArray<NSString*>* link = self.link ? self.link() : nil;
  if (!link) {
    [super mouseDown:event];
    return;
  }
  self.highlighted = YES;
  BOOL dragged = TrackDrag(self.window, event);
  self.highlighted = NO;
  if (!dragged) {
    [self sendAction:self.action to:self.target];
    return;
  }
  NSPasteboardItem* data = [[NSPasteboardItem alloc] init];
  [data setString:link[0] forType:NSPasteboardTypeURL];
  [data setString:link[0] forType:NSPasteboardTypeString];
  [data setString:link[1] forType:@"public.url-name"];
  NSDraggingItem* dragging =
      [[NSDraggingItem alloc] initWithPasteboardWriter:data];
  [dragging setDraggingFrame:self.bounds contents:SnapshotOf(self)];
  [self beginDraggingSessionWithItems:@[ dragging ] event:event source:self];
}

- (NSDragOperation)draggingSession:(NSDraggingSession*)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
  return NSDragOperationCopy | NSDragOperationLink;
}

@end
