#import "platform/mac/autofill_prompt.h"

#include <string>
#include <utility>

namespace {

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

}  // namespace

@interface BRAutofillPrompt () <NSPopoverDelegate>
@end

@implementation BRAutofillPrompt {
  NSPopover* _popover;
  NSTextField* _label;
  brouk::AutofillPromptDone _done;
  BRAutofillPrompt* _keepAlive;
}

+ (void)showPrompt:(const brouk::AutofillPrompt&)prompt
        relativeTo:(NSView*)anchor
              done:(brouk::AutofillPromptDone)done {
  [[[BRAutofillPrompt alloc] init] show:prompt
                             relativeTo:anchor
                                   done:std::move(done)];
}

- (void)show:(const brouk::AutofillPrompt&)prompt
    relativeTo:(NSView*)anchor
          done:(brouk::AutofillPromptDone)done {
  using Kind = brouk::AutofillPrompt::Kind;
  _done = std::move(done);
  _keepAlive = self;
  NSStackView* stack = [[NSStackView alloc] init];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 8;
  stack.edgeInsets = NSEdgeInsetsMake(16, 16, 16, 16);

  NSString* title =
      prompt.kind == Kind::kAddTo
          ? [NSString stringWithFormat:@"Add to “%@”?",
                                       ToNSString(prompt.entry_label)]
      : prompt.kind == Kind::kUpdate
          ? [NSString stringWithFormat:@"Update “%@”?",
                                       ToNSString(prompt.entry_label)]
          : @"Save as new entry?";
  NSTextField* heading = [NSTextField labelWithString:title];
  heading.font = [NSFont boldSystemFontOfSize:13];
  [stack addArrangedSubview:heading];

  NSGridView* grid = [NSGridView gridViewWithNumberOfColumns:2 rows:0];
  grid.rowSpacing = 4;
  grid.columnSpacing = 10;
  for (const brouk::AutofillPrompt::Line& line : prompt.lines) {
    NSTextField* name = [NSTextField labelWithString:ToNSString(line.field)];
    name.textColor = NSColor.secondaryLabelColor;
    NSString* value =
        line.old_value.empty()
            ? ToNSString(line.new_value)
            : [NSString stringWithFormat:@"%@ → %@", ToNSString(line.old_value),
                                         ToNSString(line.new_value)];
    NSTextField* text = [NSTextField wrappingLabelWithString:value];
    text.preferredMaxLayoutWidth = 260;
    if (!line.problem.empty()) {
      text.stringValue =
          [value stringByAppendingFormat:@"\n⚠︎ %@", ToNSString(line.problem)];
      text.textColor = NSColor.systemOrangeColor;
    }
    [grid addRowWithViews:@[ name, text ]];
  }
  [stack addArrangedSubview:grid];

  if (prompt.kind != Kind::kAddTo) {
    NSStackView* labelRow = [[NSStackView alloc] init];
    labelRow.hidden = prompt.kind == Kind::kUpdate;
    NSTextField* caption = [NSTextField labelWithString:@"Label"];
    caption.textColor = NSColor.secondaryLabelColor;
    _label =
        [NSTextField textFieldWithString:ToNSString(prompt.suggested_label)];
    _label.placeholderString = @"e.g. Work, Home, Company invoicing";
    [_label.widthAnchor constraintEqualToConstant:220].active = YES;
    _label.accessibilityLabel = @"Label of the new entry";
    [labelRow addArrangedSubview:caption];
    [labelRow addArrangedSubview:_label];
    [stack addArrangedSubview:labelRow];
  }

  NSStackView* buttons = [[NSStackView alloc] init];
  buttons.spacing = 8;
  NSButton* edit = [NSButton buttonWithTitle:@"Edit…"
                                      target:self
                                      action:@selector(edit:)];
  [buttons addArrangedSubview:edit];
  NSView* spacer = [[NSView alloc] init];
  [spacer setContentHuggingPriority:1
                     forOrientation:NSLayoutConstraintOrientationHorizontal];
  [buttons addArrangedSubview:spacer];
  [buttons addArrangedSubview:[NSButton buttonWithTitle:@"Not now"
                                                 target:self
                                                 action:@selector(notNow:)]];
  if (prompt.kind == Kind::kUpdate) {
    [buttons addArrangedSubview:[NSButton buttonWithTitle:@"Save as new"
                                                   target:self
                                                   action:@selector(saveNew:)]];
  }
  NSButton* accept =
      [NSButton buttonWithTitle:prompt.kind == Kind::kAddTo    ? @"Add"
                                : prompt.kind == Kind::kUpdate ? @"Update"
                                                               : @"Save"
                         target:self
                         action:@selector(accept:)];
  accept.keyEquivalent = @"\r";
  [buttons addArrangedSubview:accept];
  [stack addArrangedSubview:buttons];
  [buttons.widthAnchor constraintGreaterThanOrEqualToConstant:340].active = YES;

  NSViewController* controller = [[NSViewController alloc] init];
  controller.view = stack;
  _popover = [[NSPopover alloc] init];
  _popover.contentViewController = controller;
  _popover.behavior = NSPopoverBehaviorSemitransient;
  _popover.delegate = self;
  [_popover showRelativeToRect:anchor.bounds
                        ofView:anchor
                 preferredEdge:NSRectEdgeMaxY];
}

- (void)answer:(brouk::AutofillPromptAnswer::Choice)choice {
  brouk::AutofillPromptDone done = std::move(_done);
  _done = nullptr;
  if (done) {
    brouk::AutofillPromptAnswer answer;
    answer.choice = choice;
    answer.label = _label.stringValue.UTF8String ?: "";
    done(std::move(answer));
  }
  [_popover close];
}

- (void)accept:(id)sender {
  [self answer:brouk::AutofillPromptAnswer::Choice::kAccept];
}

- (void)saveNew:(id)sender {
  [self answer:brouk::AutofillPromptAnswer::Choice::kSaveAsNew];
}

- (void)notNow:(id)sender {
  [self answer:brouk::AutofillPromptAnswer::Choice::kNotNow];
}

- (void)edit:(id)sender {
  [self answer:brouk::AutofillPromptAnswer::Choice::kEdit];
}

- (void)popoverDidClose:(NSNotification*)notification {
  if (_done) {
    [self answer:brouk::AutofillPromptAnswer::Choice::kNotNow];
  }
  _keepAlive = nil;
}

@end
