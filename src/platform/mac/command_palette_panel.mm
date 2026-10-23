#import "platform/mac/command_palette_panel.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace {

constexpr CGFloat kWidth = 600;
constexpr CGFloat kFieldHeight = 48;
constexpr CGFloat kRowHeight = 38;
constexpr NSUInteger kVisibleRows = 10;
constexpr std::size_t kMaxResults = 50;
// From the top of the browser window: below the tab strip and toolbar.
constexpr CGFloat kTopOffset = 92;

}  // namespace

@implementation BRPaletteEntry
@end

// A borderless panel that can take the keyboard.
@interface BRPalettePanel : NSPanel
@end

@implementation BRPalettePanel
- (BOOL)canBecomeKeyWindow {
  return YES;
}
@end

// One result row: icon, title and detail, and the shortcut at the right.
@interface BRPaletteRowView : NSTableCellView
- (void)showEntry:(BRPaletteEntry*)entry;
@end

@implementation BRPaletteRowView {
  NSImageView* _icon;
  NSTextField* _title;
  NSTextField* _detail;
  NSTextField* _shortcut;
}

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    _icon = [NSImageView imageViewWithImage:[NSImage new]];
    _icon.imageScaling = NSImageScaleProportionallyUpOrDown;
    _title = [NSTextField labelWithString:@""];
    _title.font = [NSFont systemFontOfSize:13];
    _title.lineBreakMode = NSLineBreakByTruncatingTail;
    _detail = [NSTextField labelWithString:@""];
    _detail.font = [NSFont systemFontOfSize:11];
    _detail.textColor = NSColor.secondaryLabelColor;
    _detail.lineBreakMode = NSLineBreakByTruncatingMiddle;
    _shortcut = [NSTextField labelWithString:@""];
    _shortcut.font = [NSFont systemFontOfSize:12];
    _shortcut.textColor = NSColor.secondaryLabelColor;
    _shortcut.alignment = NSTextAlignmentRight;
    for (NSView* view in @[ _icon, _title, _detail, _shortcut ]) {
      [self addSubview:view];
    }
  }
  return self;
}

- (void)showEntry:(BRPaletteEntry*)entry {
  _icon.image = entry.image;
  _title.stringValue = entry.title;
  _detail.stringValue = entry.detail;
  _detail.hidden = entry.detail.length == 0;
  _shortcut.stringValue = entry.shortcut ?: @"";
  self.toolTip =
      entry.detail.length
          ? [NSString stringWithFormat:@"%@\n%@", entry.title, entry.detail]
          : entry.title;
  self.needsLayout = YES;
}

- (void)layout {
  [super layout];
  NSRect b = self.bounds;
  _icon.frame = NSMakeRect(12, (b.size.height - 18) / 2, 18, 18);
  CGFloat shortcutWidth =
      _shortcut.stringValue.length ? _shortcut.intrinsicContentSize.width : 0;
  _shortcut.frame = NSMakeRect(b.size.width - 12 - shortcutWidth,
                               (b.size.height - 16) / 2, shortcutWidth, 16);
  CGFloat textX = 40;
  CGFloat textWidth = NSMinX(_shortcut.frame) - 8 - textX;
  if (_detail.hidden) {
    _title.frame = NSMakeRect(textX, (b.size.height - 17) / 2, textWidth, 17);
  } else {
    _title.frame = NSMakeRect(textX, b.size.height / 2 - 1, textWidth, 17);
    _detail.frame = NSMakeRect(textX, b.size.height / 2 - 15, textWidth, 14);
  }
}

@end

@interface BRCommandPalette () <NSTableViewDataSource,
                                NSTableViewDelegate,
                                NSTextFieldDelegate,
                                NSWindowDelegate>
@end

@implementation BRCommandPalette {
  BRPalettePanel* _panel;
  NSTextField* _field;
  NSTableView* _table;
  NSScrollView* _scroll;
  NSArray<BRPaletteEntry*>* _entries;
  std::vector<brouk::PaletteItem> _items;
  NSArray<BRPaletteEntry*>* _results;
  __weak NSWindow* _parent;
}

- (BOOL)isVisible {
  return _panel.isVisible;
}

- (void)buildPanel {
  _panel = [[BRPalettePanel alloc]
      initWithContentRect:NSMakeRect(0, 0, kWidth, kFieldHeight)
                styleMask:NSWindowStyleMaskBorderless |
                          NSWindowStyleMaskNonactivatingPanel
                  backing:NSBackingStoreBuffered
                    defer:NO];
  _panel.opaque = NO;
  _panel.backgroundColor = NSColor.clearColor;
  _panel.hasShadow = YES;
  _panel.delegate = self;
  _panel.releasedWhenClosed = NO;

  NSVisualEffectView* background =
      [[NSVisualEffectView alloc] initWithFrame:_panel.contentView.bounds];
  background.material = NSVisualEffectMaterialMenu;
  background.state = NSVisualEffectStateActive;
  background.wantsLayer = YES;
  background.layer.cornerRadius = 10;
  background.layer.masksToBounds = YES;
  background.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  _panel.contentView = background;

  _field = [[NSTextField alloc] initWithFrame:NSZeroRect];
  _field.bezeled = NO;
  _field.drawsBackground = NO;
  _field.focusRingType = NSFocusRingTypeNone;
  _field.font = [NSFont systemFontOfSize:17];
  _field.placeholderString = @"Search actions, tabs and settings";
  _field.delegate = self;
  _field.cell.usesSingleLineMode = YES;
  _field.cell.scrollable = YES;
  _field.accessibilityLabel = @"Command palette";
  [background addSubview:_field];

  _table = [[NSTableView alloc] initWithFrame:NSZeroRect];
  NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"entry"];
  column.resizingMask = NSTableColumnAutoresizingMask;
  [_table addTableColumn:column];
  _table.headerView = nil;
  _table.rowHeight = kRowHeight;
  _table.intercellSpacing = NSMakeSize(0, 0);
  _table.backgroundColor = NSColor.clearColor;
  _table.style = NSTableViewStyleFullWidth;
  _table.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
  _table.dataSource = self;
  _table.delegate = self;
  _table.target = self;
  _table.action = @selector(rowClicked:);
  _table.refusesFirstResponder = YES;
  _table.accessibilityLabel = @"Results";

  _scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  _scroll.documentView = _table;
  _scroll.drawsBackground = NO;
  _scroll.hasVerticalScroller = YES;
  _scroll.autohidesScrollers = YES;
  [background addSubview:_scroll];
}

- (void)showInWindow:(NSWindow*)window
             entries:(NSArray<BRPaletteEntry*>*)entries {
  if (!_panel) {
    [self buildPanel];
  }
  [self close];
  _parent = window;
  _entries = [entries copy];
  _items.clear();
  for (BRPaletteEntry* entry in _entries) {
    _items.push_back({entry.kind, entry.title.UTF8String ?: "",
                      entry.detail.UTF8String ?: ""});
  }
  _field.stringValue = @"";
  [self updateResults];
  [window addChildWindow:_panel ordered:NSWindowAbove];
  [_panel makeKeyAndOrderFront:nil];
  [_panel makeFirstResponder:_field];
}

- (void)close {
  if (!_panel.isVisible) {
    return;
  }
  [_parent removeChildWindow:_panel];
  [_panel orderOut:nil];
  [_parent makeKeyAndOrderFront:nil];
}

- (void)updateResults {
  std::vector<std::size_t> ranked = brouk::RankPaletteItems(
      _items, _field.stringValue.UTF8String ?: "", kMaxResults);
  NSMutableArray<BRPaletteEntry*>* results = [NSMutableArray array];
  for (std::size_t index : ranked) {
    [results addObject:_entries[index]];
  }
  _results = results;
  [_table reloadData];
  if (_results.count > 0) {
    [_table selectRowIndexes:[NSIndexSet indexSetWithIndex:0]
        byExtendingSelection:NO];
    [_table scrollRowToVisible:0];
  }
  [self updateFrame];
}

// Grows with the results, up to kVisibleRows, under the parent's toolbar.
- (void)updateFrame {
  NSWindow* parent = _parent;
  if (!parent) {
    return;
  }
  NSUInteger rows = std::min<NSUInteger>(_results.count, kVisibleRows);
  CGFloat listHeight = rows > 0 ? rows * kRowHeight + 8 : 0;
  CGFloat width = std::min(kWidth, parent.frame.size.width - 40);
  CGFloat height = kFieldHeight + listHeight;
  NSRect frame = parent.frame;
  NSRect panelFrame =
      NSMakeRect(NSMidX(frame) - width / 2, NSMaxY(frame) - kTopOffset - height,
                 width, height);
  [_panel setFrame:panelFrame display:YES];
  NSRect content = _panel.contentView.bounds;
  _field.frame = NSMakeRect(16, content.size.height - kFieldHeight + 12,
                            content.size.width - 32, 24);
  _scroll.frame = NSMakeRect(0, 4, content.size.width, listHeight);
  _scroll.hidden = rows == 0;
  _table.tableColumns.firstObject.width = content.size.width;
}

- (void)runSelected {
  NSInteger row = _table.selectedRow;
  if (row < 0 || row >= static_cast<NSInteger>(_results.count)) {
    return;
  }
  BRPaletteEntry* entry = _results[static_cast<NSUInteger>(row)];
  [self close];
  if (entry.run) {
    entry.run();
  }
}

- (void)moveSelectionBy:(NSInteger)delta {
  NSInteger count = static_cast<NSInteger>(_results.count);
  if (count == 0) {
    return;
  }
  NSInteger row =
      std::clamp<NSInteger>(_table.selectedRow + delta, 0, count - 1);
  [_table selectRowIndexes:[NSIndexSet
                               indexSetWithIndex:static_cast<NSUInteger>(row)]
      byExtendingSelection:NO];
  [_table scrollRowToVisible:row];
}

- (void)rowClicked:(id)sender {
  if (_table.clickedRow >= 0) {
    [self runSelected];
  }
}

#pragma mark - NSTextFieldDelegate

- (void)controlTextDidChange:(NSNotification*)notification {
  [self updateResults];
}

- (BOOL)control:(NSControl*)control
               textView:(NSTextView*)textView
    doCommandBySelector:(SEL)command {
  if (command == @selector(moveDown:)) {
    [self moveSelectionBy:1];
  } else if (command == @selector(moveUp:)) {
    [self moveSelectionBy:-1];
  } else if (command == @selector(pageDown:) ||
             command == @selector(scrollPageDown:)) {
    [self moveSelectionBy:static_cast<NSInteger>(kVisibleRows)];
  } else if (command == @selector(pageUp:) ||
             command == @selector(scrollPageUp:)) {
    [self moveSelectionBy:-static_cast<NSInteger>(kVisibleRows)];
  } else if (command == @selector(insertNewline:)) {
    [self runSelected];
  } else if (command == @selector(cancelOperation:)) {
    [self close];
  } else {
    return NO;
  }
  return YES;
}

#pragma mark - NSTableView

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
  return static_cast<NSInteger>(_results.count);
}

- (nullable NSView*)tableView:(NSTableView*)tableView
           viewForTableColumn:(nullable NSTableColumn*)tableColumn
                          row:(NSInteger)row {
  BRPaletteRowView* view = [tableView makeViewWithIdentifier:@"row" owner:self];
  if (!view) {
    view = [[BRPaletteRowView alloc] initWithFrame:NSZeroRect];
    view.identifier = @"row";
  }
  [view showEntry:_results[static_cast<NSUInteger>(row)]];
  return view;
}

#pragma mark - NSWindowDelegate

- (void)windowDidResignKey:(NSNotification*)notification {
  [self close];
}

@end
