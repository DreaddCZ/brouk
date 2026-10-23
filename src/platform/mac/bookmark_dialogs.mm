#import "platform/mac/bookmark_dialogs.h"

#include <string>
#include <utility>
#include <vector>

#import "platform/mac/profile_colors.h"

#include "core/address_input.h"

namespace {

constexpr NSInteger kNewFolderTag = -1;
constexpr CGFloat kFieldWidth = 260;

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

NSTextField* Label(NSString* text) {
  NSTextField* label = [NSTextField labelWithString:text];
  label.alignment = NSTextAlignmentRight;
  return label;
}

}  // namespace

// The fields both dialogs share: name, URL, folder and profile, with the
// profile locked to the folder's when the folder is linked (011).
@interface BRBookmarkFields : NSObject
@property(nonatomic, readonly) NSTextField* nameField;
@property(nonatomic, readonly) NSTextField* urlField;
@property(nonatomic, readonly) NSPopUpButton* folderPopup;
@property(nonatomic, readonly) NSPopUpButton* profilePopup;
@property(nonatomic, readonly) NSGridView* grid;
@end

@implementation BRBookmarkFields {
  BRBookmarkStore* _store;
  brouk::ProfileId _activeProfile;
  brouk::BookmarkId _editing;
  brouk::ProfileId _ownProfile;  // The choice while the folder does not lock.
}

- (instancetype)initWithStore:(BRBookmarkStore*)store
                     profiles:(const std::vector<brouk::ProfileInfo>&)profiles
                activeProfile:(const brouk::ProfileId&)activeProfile
                      editing:(brouk::BookmarkId)editing
                      showURL:(BOOL)showURL
                   showFolder:(BOOL)showFolder
                       values:(const BookmarkEditorValues&)values {
  if ((self = [super init])) {
    _store = store;
    _activeProfile = activeProfile;
    _editing = editing;
    _ownProfile = values.profile;

    _nameField = [NSTextField textFieldWithString:ToNSString(values.name)];
    _nameField.accessibilityLabel = @"Name";
    _urlField = [NSTextField textFieldWithString:ToNSString(values.url)];
    _urlField.accessibilityLabel = @"URL";
    _folderPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect
                                              pullsDown:NO];
    _folderPopup.accessibilityLabel = @"Folder";
    _folderPopup.target = self;
    _folderPopup.action = @selector(folderChosen:);
    _profilePopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect
                                               pullsDown:NO];
    _profilePopup.accessibilityLabel = @"Profile";
    _profilePopup.target = self;
    _profilePopup.action = @selector(profileChosen:);
    [_profilePopup addItemWithTitle:@"General (all profiles)"];
    _profilePopup.lastItem.representedObject = @"";
    for (const brouk::ProfileInfo& profile : profiles) {
      [_profilePopup addItemWithTitle:ToNSString(profile.name)];
      _profilePopup.lastItem.representedObject = ToNSString(profile.id);
      _profilePopup.lastItem.image = brouk::mac::ProfileDot(profile.color);
    }
    [self fillFoldersSelecting:values.folder];

    NSMutableArray<NSArray<NSView*>*>* rows = [NSMutableArray array];
    [rows addObject:@[ Label(@"Name:"), _nameField ]];
    if (showURL) {
      [rows addObject:@[ Label(@"URL:"), _urlField ]];
    }
    if (showFolder) {
      [rows addObject:@[ Label(@"Folder:"), _folderPopup ]];
    }
    [rows addObject:@[ Label(@"Profile:"), _profilePopup ]];
    _grid = [NSGridView gridViewWithViews:rows];
    _grid.rowSpacing = 8;
    _grid.columnSpacing = 8;
    _grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
    [_grid columnAtIndex:1].width = kFieldWidth;
    [self updateProfile];
  }
  return self;
}

- (void)fillFoldersSelecting:(brouk::BookmarkId)selected {
  [_folderPopup removeAllItems];
  const brouk::BookmarkModel& model = [_store model];
  for (const brouk::BookmarkFolderEntry& entry :
       model.Folders(_activeProfile)) {
    if (_editing != 0 && model.IsInside(entry.id, _editing)) {
      continue;  // Not into itself.
    }
    const brouk::BookmarkItem* folder = model.Find(entry.id);
    [_folderPopup addItemWithTitle:@""];
    NSMenuItem* item = _folderPopup.lastItem;
    // Titles may repeat; set after adding (addItemWithTitle merges equal
    // titles).
    item.title = ToNSString(folder->name);
    item.indentationLevel = entry.depth;
    item.tag = static_cast<NSInteger>(entry.id);
    item.image = [NSImage imageWithSystemSymbolName:@"folder"
                           accessibilityDescription:nil];
  }
  [_folderPopup.menu addItem:NSMenuItem.separatorItem];
  [_folderPopup addItemWithTitle:@"New Folder…"];
  _folderPopup.lastItem.tag = kNewFolderTag;
  NSInteger index =
      [_folderPopup indexOfItemWithTag:static_cast<NSInteger>(selected)];
  if (index < 0) {
    index = [_folderPopup indexOfItemWithTag:static_cast<NSInteger>(
                                                 brouk::BookmarkModel::kBarId)];
  }
  [_folderPopup selectItemAtIndex:index];
}

- (brouk::BookmarkId)folder {
  return static_cast<brouk::BookmarkId>(_folderPopup.selectedTag);
}

- (std::string)name {
  return _nameField.stringValue.UTF8String ?: "";
}

- (brouk::ProfileId)profile {
  NSString* profile = _profilePopup.selectedItem.representedObject;
  return [profile isKindOfClass:NSString.class] ? profile.UTF8String : "";
}

// The folder's profile, when it (or a folder above it) is linked.
- (brouk::ProfileId)lockedProfile {
  return [_store model].EffectiveProfile([self folder]);
}

- (void)selectProfile:(const brouk::ProfileId&)profile {
  NSInteger index =
      [_profilePopup indexOfItemWithRepresentedObject:ToNSString(profile)];
  [_profilePopup selectItemAtIndex:index < 0 ? 0 : index];
}

- (void)updateProfile {
  brouk::ProfileId locked = [self lockedProfile];
  _profilePopup.enabled = locked.empty();
  [self selectProfile:locked.empty() ? _ownProfile : locked];
  _profilePopup.toolTip =
      locked.empty() ? @"General bookmarks show in every profile."
                     : @"The folder is linked to this profile; everything in "
                       @"it follows the folder.";
}

- (void)profileChosen:(id)sender {
  _ownProfile = [self profile];
}

- (void)folderChosen:(id)sender {
  if (_folderPopup.selectedTag != kNewFolderTag) {
    [self rememberFolder];
    [self updateProfile];
    return;
  }
  // A new folder inside the folder chosen before.
  auto parent = static_cast<brouk::BookmarkId>(_folderPopup.tag);
  NSAlert* alert = [[NSAlert alloc] init];
  alert.messageText = @"New Folder";
  alert.informativeText = @"Name of the new folder:";
  NSTextField* field = [NSTextField textFieldWithString:@"New folder"];
  field.frame = NSMakeRect(0, 0, kFieldWidth, 22);
  alert.accessoryView = field;
  [alert addButtonWithTitle:@"Create"];
  [alert addButtonWithTitle:@"Cancel"];
  alert.window.initialFirstResponder = field;
  brouk::BookmarkId created = 0;
  if ([alert runModal] == NSAlertFirstButtonReturn &&
      field.stringValue.length > 0) {
    std::string name = field.stringValue.UTF8String ?: "";
    [_store change:[&](brouk::BookmarkModel& model) {
      brouk::Result<brouk::BookmarkId> id = model.AddFolder(
          parent, brouk::BookmarkModel::kEnd, name, "",
          static_cast<std::int64_t>(NSDate.date.timeIntervalSince1970));
      if (id) {
        created = id.value();
      }
    }];
  }
  [self fillFoldersSelecting:created != 0 ? created : parent];
  [self rememberFolder];
  [self updateProfile];
}

// Remembers the folder chosen, for "New Folder…" (the popup's own tag).
- (void)rememberFolder {
  _folderPopup.tag = _folderPopup.selectedTag;
}

@end

#pragma mark - Bubble

@interface BRBookmarkBubbleController
    : NSViewController <NSPopoverDelegate, NSTextFieldDelegate>
@property(nonatomic, weak) NSPopover* popover;
@end

@implementation BRBookmarkBubbleController {
  BRBookmarkStore* _store;
  brouk::BookmarkId _bookmark;
  BOOL _newlyAdded;
  BOOL _removed;
  BRBookmarkFields* _fields;
  void (^_removedBlock)(void);
}

- (instancetype)initWithBookmark:(brouk::BookmarkId)bookmark
                           store:(BRBookmarkStore*)store
                        profiles:
                            (const std::vector<brouk::ProfileInfo>&)profiles
                   activeProfile:(const brouk::ProfileId&)activeProfile
                      newlyAdded:(BOOL)newlyAdded
                         removed:(void (^)(void))removed {
  if ((self = [super initWithNibName:nil bundle:nil])) {
    _store = store;
    _bookmark = bookmark;
    _newlyAdded = newlyAdded;
    _removedBlock = removed;
    const brouk::BookmarkItem* item = [store model].Find(bookmark);
    BookmarkEditorValues values;
    if (item) {
      values = {.name = item->name,
                .url = item->url,
                .folder = item->parent,
                .profile = item->profile};
    }
    _fields = [[BRBookmarkFields alloc] initWithStore:store
                                             profiles:profiles
                                        activeProfile:activeProfile
                                              editing:bookmark
                                              showURL:NO
                                           showFolder:YES
                                               values:values];
    [_fields rememberFolder];
  }
  return self;
}

- (void)loadView {
  NSTextField* title = [NSTextField
      labelWithString:_newlyAdded ? @"Bookmark added" : @"Edit bookmark"];
  title.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize + 1];

  NSButton* remove = [NSButton buttonWithTitle:@"Remove"
                                        target:self
                                        action:@selector(remove:)];
  NSButton* done = [NSButton buttonWithTitle:@"Done"
                                      target:self
                                      action:@selector(done:)];
  done.keyEquivalent = @"\r";
  NSStackView* buttons = [NSStackView stackViewWithViews:@[ remove, done ]];
  buttons.spacing = 8;

  NSStackView* stack =
      [NSStackView stackViewWithViews:@[ title, _fields.grid, buttons ]];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeTrailing;
  stack.spacing = 12;
  stack.edgeInsets = NSEdgeInsetsMake(16, 16, 16, 16);
  [stack setCustomSpacing:12 afterView:title];
  // The title stays on the left.
  [title.leadingAnchor constraintEqualToAnchor:stack.leadingAnchor constant:16]
      .active = YES;
  self.view = stack;
}

- (void)viewDidAppear {
  [super viewDidAppear];
  [self.view.window makeFirstResponder:_fields.nameField];
  [_fields.nameField selectText:nil];
}

- (void)remove:(id)sender {
  _removed = YES;
  [_store removeItems:{_bookmark}];
  [self.popover close];
  if (_removedBlock) {
    _removedBlock();
  }
}

- (void)done:(id)sender {
  [self.popover close];
}

// Closing keeps the bookmark (011) with what was changed.
- (void)popoverWillClose:(NSNotification*)notification {
  if (_removed || ![_store model].Find(_bookmark)) {
    return;
  }
  std::string name = [_fields name];
  brouk::BookmarkId folder = [_fields folder];
  brouk::ProfileId profile = [_fields profile];
  brouk::BookmarkId bookmark = _bookmark;
  [_store change:[&](brouk::BookmarkModel& model) {
    const brouk::BookmarkItem* item = model.Find(bookmark);
    if (item->name != name) {
      model.Rename(bookmark, name);
    }
    if (folder > 0 && item->parent != folder && model.Find(folder)) {
      model.Move(bookmark, folder, brouk::BookmarkModel::kEnd);
    }
    if (model.InheritedProfile(bookmark).empty() &&
        model.Find(bookmark)->profile != profile) {
      model.SetProfile(bookmark, profile);
    }
  }];
  if (folder > 0 && [_store model].Find(folder)) {
    _store.lastUsedFolder = folder;
  }
}

@end

@implementation BRBookmarkBubble

+ (void)showForBookmark:(brouk::BookmarkId)bookmarkId
                  store:(BRBookmarkStore*)store
               profiles:(const std::vector<brouk::ProfileInfo>&)profiles
          activeProfile:(const brouk::ProfileId&)activeProfile
             newlyAdded:(BOOL)newlyAdded
             relativeTo:(NSView*)anchor
                removed:(void (^)(void))removed {
  BRBookmarkBubbleController* controller =
      [[BRBookmarkBubbleController alloc] initWithBookmark:bookmarkId
                                                     store:store
                                                  profiles:profiles
                                             activeProfile:activeProfile
                                                newlyAdded:newlyAdded
                                                   removed:removed];
  NSPopover* popover = [[NSPopover alloc] init];
  popover.behavior = NSPopoverBehaviorTransient;
  popover.contentViewController = controller;
  popover.delegate = controller;
  controller.popover = popover;
  [popover showRelativeToRect:anchor.bounds
                       ofView:anchor
                preferredEdge:NSRectEdgeMaxY];
}

@end

#pragma mark - Undo notice

namespace {
constexpr NSTimeInterval kUndoNoticeSeconds = 6;
}  // namespace

@interface BRBookmarkUndoController : NSViewController
@property(nonatomic, weak) NSPopover* popover;
@end

@implementation BRBookmarkUndoController {
  BRBookmarkStore* _store;
  NSString* _message;
}

- (instancetype)initWithMessage:(NSString*)message
                          store:(BRBookmarkStore*)store {
  if ((self = [super initWithNibName:nil bundle:nil])) {
    _message = message;
    _store = store;
  }
  return self;
}

- (void)loadView {
  NSTextField* label = [NSTextField labelWithString:_message];
  NSButton* undo = [NSButton buttonWithTitle:@"Undo"
                                      target:self
                                      action:@selector(undo:)];
  NSStackView* stack = [NSStackView stackViewWithViews:@[ label, undo ]];
  stack.spacing = 12;
  stack.edgeInsets = NSEdgeInsetsMake(10, 14, 10, 10);
  self.view = stack;
}

- (void)undo:(id)sender {
  [_store undoRemove];
  [self.popover close];
}

@end

@implementation BRBookmarkUndoNotice

+ (void)showMessage:(NSString*)message
              store:(BRBookmarkStore*)store
         relativeTo:(NSView*)anchor {
  BRBookmarkUndoController* controller =
      [[BRBookmarkUndoController alloc] initWithMessage:message store:store];
  NSPopover* popover = [[NSPopover alloc] init];
  popover.behavior = NSPopoverBehaviorTransient;
  popover.contentViewController = controller;
  controller.popover = popover;
  [popover showRelativeToRect:anchor.bounds
                       ofView:anchor
                preferredEdge:NSRectEdgeMaxY];
  __weak NSPopover* weakPopover = popover;
  dispatch_after(
      dispatch_time(DISPATCH_TIME_NOW,
                    static_cast<int64_t>(kUndoNoticeSeconds * NSEC_PER_SEC)),
      dispatch_get_main_queue(), ^{
        [weakPopover close];
      });
}

@end

#pragma mark - Edit dialog

@interface BRBookmarkEditorController : NSWindowController
@end

@implementation BRBookmarkEditorController {
  BookmarkEditorKind _kind;
  BRBookmarkFields* _fields;
  NSTextField* _error;
  void (^_done)(const BookmarkEditorValues&);
  // Keeps itself alive while the sheet is shown.
  BRBookmarkEditorController* _selfReference;
}

- (instancetype)initWithKind:(BookmarkEditorKind)kind
                       title:(NSString*)title
                      values:(const BookmarkEditorValues&)values
                     editing:(brouk::BookmarkId)editing
                       store:(BRBookmarkStore*)store
                    profiles:(const std::vector<brouk::ProfileInfo>&)profiles
               activeProfile:(const brouk::ProfileId&)activeProfile
                        done:(void (^)(const BookmarkEditorValues&))done {
  NSPanel* panel =
      [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 380, 200)
                                 styleMask:NSWindowStyleMaskTitled
                                   backing:NSBackingStoreBuffered
                                     defer:YES];
  if ((self = [super initWithWindow:panel])) {
    _kind = kind;
    _done = done;
    BOOL bookmark = kind == BookmarkEditorKind::kBookmark;
    _fields = [[BRBookmarkFields alloc]
        initWithStore:store
             profiles:profiles
        activeProfile:activeProfile
              editing:editing
              showURL:bookmark
           showFolder:kind != BookmarkEditorKind::kFolder
               values:values];
    [_fields rememberFolder];

    NSTextField* heading = [NSTextField labelWithString:title];
    heading.font = [NSFont boldSystemFontOfSize:NSFont.systemFontSize + 1];
    _error = [NSTextField labelWithString:@""];
    _error.textColor = NSColor.systemRedColor;
    _error.hidden = YES;
    NSButton* cancel = [NSButton buttonWithTitle:@"Cancel"
                                          target:self
                                          action:@selector(cancel:)];
    cancel.keyEquivalent = @"\033";
    NSButton* save = [NSButton buttonWithTitle:@"Save"
                                        target:self
                                        action:@selector(save:)];
    save.keyEquivalent = @"\r";
    NSStackView* buttons = [NSStackView stackViewWithViews:@[ cancel, save ]];
    NSStackView* stack = [NSStackView
        stackViewWithViews:@[ heading, _fields.grid, _error, buttons ]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeTrailing;
    stack.spacing = 12;
    stack.edgeInsets = NSEdgeInsetsMake(20, 20, 20, 20);
    [heading.leadingAnchor constraintEqualToAnchor:stack.leadingAnchor
                                          constant:20]
        .active = YES;
    panel.contentView = stack;
    panel.initialFirstResponder = _fields.nameField;
  }
  return self;
}

- (void)beginOn:(NSWindow*)window {
  _selfReference = self;
  [window beginSheet:self.window completionHandler:nil];
}

- (void)finish {
  [self.window.sheetParent endSheet:self.window];
  _selfReference = nil;
}

- (void)cancel:(id)sender {
  [self finish];
}

- (void)save:(id)sender {
  BookmarkEditorValues values{.name = [_fields name],
                              .url = "",
                              .folder = [_fields folder],
                              .profile = [_fields profile]};
  if (_kind == BookmarkEditorKind::kBookmark) {
    brouk::AddressInputResult url = brouk::ClassifyAddressInput(
        _fields.urlField.stringValue.UTF8String ?: "");
    if (url.kind != brouk::AddressInputResult::Kind::kUrl) {
      _error.stringValue = @"Enter a web address, e.g. https://example.com";
      _error.hidden = NO;
      [self.window makeFirstResponder:_fields.urlField];
      return;
    }
    values.url = url.url;
  }
  if (![_fields lockedProfile].empty()) {
    values.profile.clear();  // It follows the folder.
  }
  void (^done)(const BookmarkEditorValues&) = _done;
  [self finish];
  done(values);
}

@end

@implementation BRBookmarkEditor

+ (void)runKind:(BookmarkEditorKind)kind
            title:(NSString*)title
           values:(const BookmarkEditorValues&)values
          editing:(brouk::BookmarkId)editing
            store:(BRBookmarkStore*)store
         profiles:(const std::vector<brouk::ProfileInfo>&)profiles
    activeProfile:(const brouk::ProfileId&)activeProfile
           window:(NSWindow*)window
             done:(void (^)(const BookmarkEditorValues&))done {
  BRBookmarkEditorController* controller =
      [[BRBookmarkEditorController alloc] initWithKind:kind
                                                 title:title
                                                values:values
                                               editing:editing
                                                 store:store
                                              profiles:profiles
                                         activeProfile:activeProfile
                                                  done:done];
  [controller beginOn:window];
}

@end
