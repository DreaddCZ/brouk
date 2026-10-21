#import "platform/mac/sad_tab_view.h"

#include <algorithm>
#include <cmath>

@implementation BRSadTabView {
  void (^_reload)(void);
  NSStackView* _stack;
}

- (instancetype)initWithMessage:(NSString*)message
                      errorCode:(NSString*)errorCode
                         reload:(void (^)(void))reload {
  if ((self = [super initWithFrame:NSZeroRect])) {
    _reload = reload;
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    NSImageView* icon = [NSImageView
        imageViewWithImage:
            [NSImage imageWithSystemSymbolName:@"exclamationmark.triangle"
                      accessibilityDescription:nil]];
    icon.symbolConfiguration = [NSImageSymbolConfiguration
        configurationWithPointSize:44
                            weight:NSFontWeightLight];
    icon.contentTintColor = NSColor.secondaryLabelColor;

    NSTextField* title = [NSTextField labelWithString:@"Aw, Snap!"];
    title.font = [NSFont systemFontOfSize:22 weight:NSFontWeightSemibold];
    NSTextField* text = [NSTextField wrappingLabelWithString:message];
    text.textColor = NSColor.secondaryLabelColor;
    NSTextField* code = [NSTextField labelWithString:errorCode];
    code.font = [NSFont systemFontOfSize:11];
    code.textColor = NSColor.tertiaryLabelColor;
    code.hidden = errorCode.length == 0;
    NSButton* button = [NSButton buttonWithTitle:@"Reload"
                                          target:self
                                          action:@selector(reloadClicked:)];
    button.keyEquivalent = @"\r";

    _stack =
        [NSStackView stackViewWithViews:@[ icon, title, text, code, button ]];
    _stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    _stack.alignment = NSLayoutAttributeLeading;
    _stack.spacing = 10;
    [_stack setCustomSpacing:24 afterView:code];
    [self addSubview:_stack];
    self.accessibilityLabel = @"This page crashed";
  }
  return self;
}

- (void)drawRect:(NSRect)dirtyRect {
  // Fill only our bounds (views do not clip to bounds since macOS 14).
  [NSColor.windowBackgroundColor setFill];
  NSRectFill(self.bounds);
}

- (void)layout {
  [super layout];
  NSRect b = self.bounds;
  CGFloat width = std::min<CGFloat>(480, b.size.width - 40);
  NSSize size = [_stack fittingSize];
  _stack.frame = NSMakeRect(std::floor((b.size.width - width) / 2),
                            std::floor((b.size.height - size.height) * 0.55),
                            width, size.height);
}

- (void)reloadClicked:(id)sender {
  if (_reload) {
    _reload();
  }
}

@end
