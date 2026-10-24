#import "platform/mac/adblock_blocked_view.h"

#include <algorithm>
#include <cmath>

@implementation BRAdblockBlockedView {
  void (^_allow)(void);
  void (^_goBack)(void);
  NSStackView* _stack;
}

- (instancetype)initWithHost:(NSString*)host
                    listName:(NSString*)listName
                        rule:(NSString*)rule
                       allow:(void (^)(void))allow
                      goBack:(void (^)(void))goBack {
  if ((self = [super initWithFrame:NSZeroRect])) {
    _allow = allow;
    _goBack = goBack;
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    NSImageView* icon = [NSImageView
        imageViewWithImage:
            [NSImage imageWithSystemSymbolName:@"shield.lefthalf.filled"
                      accessibilityDescription:nil]];
    icon.symbolConfiguration = [NSImageSymbolConfiguration
        configurationWithPointSize:44
                            weight:NSFontWeightLight];
    icon.contentTintColor = NSColor.secondaryLabelColor;

    NSTextField* title =
        [NSTextField labelWithString:@"Brouk blocked this page"];
    title.font = [NSFont systemFontOfSize:22 weight:NSFontWeightSemibold];
    NSTextField* text = [NSTextField
        wrappingLabelWithString:
            [NSString stringWithFormat:@"%@ is blocked by the list “%@”.", host,
                                       listName]];
    text.textColor = NSColor.secondaryLabelColor;
    NSTextField* ruleText = [NSTextField
        labelWithString:[NSString stringWithFormat:@"Rule: %@", rule]];
    ruleText.font = [NSFont monospacedSystemFontOfSize:11
                                                weight:NSFontWeightRegular];
    ruleText.textColor = NSColor.tertiaryLabelColor;
    ruleText.lineBreakMode = NSLineBreakByTruncatingMiddle;
    ruleText.hidden = rule.length == 0;
    NSButton* back = [NSButton buttonWithTitle:@"Go back"
                                        target:self
                                        action:@selector(goBackClicked:)];
    back.keyEquivalent = @"\r";
    NSButton* allowButton = [NSButton buttonWithTitle:@"Allow this site"
                                               target:self
                                               action:@selector(allowClicked:)];
    allowButton.toolTip = @"Turns blocking off for this site in this profile.";
    NSStackView* buttons =
        [NSStackView stackViewWithViews:@[ back, allowButton ]];

    _stack = [NSStackView
        stackViewWithViews:@[ icon, title, text, ruleText, buttons ]];
    _stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    _stack.alignment = NSLayoutAttributeLeading;
    _stack.spacing = 10;
    [_stack setCustomSpacing:24 afterView:ruleText];
    [self addSubview:_stack];
    self.accessibilityLabel = @"Page blocked";
  }
  return self;
}

- (void)drawRect:(NSRect)dirtyRect {
  [NSColor.windowBackgroundColor setFill];
  NSRectFill(self.bounds);
}

- (void)layout {
  [super layout];
  NSRect b = self.bounds;
  CGFloat width = std::min<CGFloat>(520, b.size.width - 40);
  NSSize size = [_stack fittingSize];
  _stack.frame = NSMakeRect(std::floor((b.size.width - width) / 2),
                            std::floor((b.size.height - size.height) * 0.55),
                            width, size.height);
}

- (void)allowClicked:(id)sender {
  if (_allow) {
    _allow();
  }
}

- (void)goBackClicked:(id)sender {
  if (_goBack) {
    _goBack();
  }
}

@end
