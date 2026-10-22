#import "platform/mac/site_info_popover.h"

#import <SecurityInterface/SFCertificatePanel.h>

#include <string>
#include <vector>

#include "cef/site_info.h"
#include "core/permissions.h"
#include "core/security_state.h"

namespace {

constexpr CGFloat kWidth = 320;
constexpr CGFloat kPadding = 16;

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

NSString* SymbolFor(brouk::SecurityLevel level) {
  switch (level) {
    case brouk::SecurityLevel::kSecure:
      return @"lock.fill";
    case brouk::SecurityLevel::kNotSecure:
      return @"exclamationmark.triangle";
    case brouk::SecurityLevel::kDangerous:
      return @"exclamationmark.triangle.fill";
    case brouk::SecurityLevel::kNone:
      return @"info.circle";
  }
  return @"info.circle";
}

}  // namespace

// Popover content. Keeps the browser so permission changes apply to it.
@interface BRSiteInfoController : NSViewController
- (instancetype)initWithBrowser:(CefRefPtr<CefBrowser>)browser
                         reload:(void (^)(void))reload;
@end

@implementation BRSiteInfoController {
  CefRefPtr<CefBrowser> _browser;
  brouk::SiteInfo _info;
  std::vector<brouk::SitePermission> _permissions;
  void (^_reload)(void);
  NSTextField* _reloadHint;
  NSButton* _reloadButton;
}

- (instancetype)initWithBrowser:(CefRefPtr<CefBrowser>)browser
                         reload:(void (^)(void))reload {
  if ((self = [super initWithNibName:nil bundle:nil])) {
    _browser = browser;
    _reload = reload;
    _info = brouk::GetSiteInfo(browser);
    _permissions = brouk::GetSitePermissions(browser, _info.origin);
  }
  return self;
}

- (void)loadView {
  NSStackView* stack = [[NSStackView alloc] init];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 8;
  stack.edgeInsets = NSEdgeInsetsMake(kPadding, kPadding, kPadding, kPadding);

  // Connection.
  NSImageView* icon = [NSImageView
      imageViewWithImage:[NSImage
                             imageWithSystemSymbolName:SymbolFor(_info.level)
                              accessibilityDescription:nil]];
  if (_info.level == brouk::SecurityLevel::kDangerous) {
    icon.contentTintColor = NSColor.systemRedColor;
  }
  NSTextField* summary = [NSTextField
      labelWithString:ToNSString(brouk::SecuritySummary(_info.level))];
  summary.font = [NSFont boldSystemFontOfSize:13];
  NSStackView* header = [NSStackView stackViewWithViews:@[ icon, summary ]];
  [stack addArrangedSubview:header];
  if (!_info.origin.empty()) {
    NSTextField* origin =
        [NSTextField labelWithString:ToNSString(_info.origin)];
    origin.textColor = NSColor.secondaryLabelColor;
    origin.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [stack addArrangedSubview:origin];
  }
  if (_info.level == brouk::SecurityLevel::kNotSecure) {
    NSTextField* warning = [NSTextField
        wrappingLabelWithString:@"Don't enter passwords or other sensitive "
                                @"information on this site."];
    warning.preferredMaxLayoutWidth = kWidth - 2 * kPadding;
    [stack addArrangedSubview:warning];
  }
  if (!_info.certificate_chain.empty()) {
    NSButton* cert = [NSButton buttonWithTitle:@"Certificate…"
                                        target:self
                                        action:@selector(showCertificate:)];
    [stack addArrangedSubview:cert];
  }

  // Permissions.
  if (!_info.origin.empty()) {
    NSBox* line = [[NSBox alloc] init];
    line.boxType = NSBoxSeparator;
    [stack addArrangedSubview:line];
    [line.widthAnchor constraintEqualToConstant:kWidth - 2 * kPadding].active =
        YES;
    NSTextField* title = [NSTextField labelWithString:@"Permissions"];
    title.font = [NSFont boldSystemFontOfSize:13];
    [stack addArrangedSubview:title];
    if (_permissions.empty()) {
      NSTextField* none =
          [NSTextField labelWithString:@"This site has no permissions set."];
      none.textColor = NSColor.secondaryLabelColor;
      [stack addArrangedSubview:none];
    }
    for (std::size_t i = 0; i < _permissions.size(); ++i) {
      [stack addArrangedSubview:[self rowForPermission:i]];
    }
    if (!_permissions.empty()) {
      NSButton* reset = [NSButton buttonWithTitle:@"Reset permissions"
                                           target:self
                                           action:@selector(resetPermissions:)];
      [stack addArrangedSubview:reset];
    }
    _reloadHint = [NSTextField labelWithString:@"Reload to apply the changes."];
    _reloadHint.textColor = NSColor.secondaryLabelColor;
    _reloadHint.hidden = YES;
    _reloadButton = [NSButton buttonWithTitle:@"Reload"
                                       target:self
                                       action:@selector(reloadPage:)];
    _reloadButton.hidden = YES;
    [stack addArrangedSubview:_reloadHint];
    [stack addArrangedSubview:_reloadButton];
  }
  [stack.widthAnchor constraintEqualToConstant:kWidth].active = YES;
  self.view = stack;
}

- (NSView*)rowForPermission:(std::size_t)index {
  const brouk::SitePermission& permission = _permissions[index];
  NSTextField* name = [NSTextField
      labelWithString:ToNSString(brouk::PermissionName(permission.kind))];
  NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect
                                                    pullsDown:NO];
  [popup addItemsWithTitles:@[ @"Ask (default)", @"Allow", @"Block" ]];
  [popup selectItemAtIndex:static_cast<NSInteger>(permission.setting)];
  popup.tag = static_cast<NSInteger>(index);
  popup.target = self;
  popup.action = @selector(permissionChanged:);
  popup.accessibilityLabel = name.stringValue;
  [name.widthAnchor constraintEqualToConstant:140].active = YES;
  return [NSStackView stackViewWithViews:@[ name, popup ]];
}

- (void)permissionChanged:(NSPopUpButton*)sender {
  auto index = static_cast<std::size_t>(sender.tag);
  if (index >= _permissions.size()) {
    return;
  }
  auto setting =
      static_cast<brouk::PermissionSetting>(sender.indexOfSelectedItem);
  brouk::SetSitePermission(_browser, _info.origin, _permissions[index].kind,
                           setting);
  [self showReloadHint];
}

- (void)resetPermissions:(id)sender {
  brouk::ResetSitePermissions(_browser, _info.origin);
  for (NSView* view in self.view.subviews) {
    for (NSView* sub in view.subviews) {
      if ([sub isKindOfClass:[NSPopUpButton class]]) {
        [static_cast<NSPopUpButton*>(sub) selectItemAtIndex:0];
      }
    }
  }
  [self showReloadHint];
}

- (void)showReloadHint {
  _reloadHint.hidden = NO;
  _reloadButton.hidden = NO;
}

- (void)reloadPage:(id)sender {
  if (_reload) {
    _reload();
  }
  [self.view.window close];
}

- (void)showCertificate:(id)sender {
  NSMutableArray* certificates = [NSMutableArray array];
  for (const std::vector<std::uint8_t>& der : _info.certificate_chain) {
    NSData* data = [NSData dataWithBytes:der.data() length:der.size()];
    SecCertificateRef cert =
        SecCertificateCreateWithData(nullptr, (__bridge CFDataRef)data);
    if (cert) {
      [certificates addObject:(__bridge_transfer id)cert];
    }
  }
  if (certificates.count == 0) {
    return;
  }
  NSWindow* window = NSApp.keyWindow;
  [self.view.window close];
  SFCertificatePanel* panel = [SFCertificatePanel sharedCertificatePanel];
  [panel beginSheetForWindow:window
               modalDelegate:nil
              didEndSelector:nil
                 contextInfo:nil
                certificates:certificates
                   showGroup:YES];
}

@end

@implementation BRSiteInfoPopover

+ (void)showForBrowser:(CefRefPtr<CefBrowser>)browser
            relativeTo:(NSView*)anchor
                reload:(void (^)(void))reload {
  if (!browser) {
    return;
  }
  NSPopover* popover = [[NSPopover alloc] init];
  popover.behavior = NSPopoverBehaviorTransient;
  popover.contentViewController =
      [[BRSiteInfoController alloc] initWithBrowser:browser reload:reload];
  [popover showRelativeToRect:anchor.bounds
                       ofView:anchor
                preferredEdge:NSRectEdgeMaxY];
}

+ (NSString*)symbolForBrowser:(CefRefPtr<CefBrowser>)browser {
  if (!browser) {
    return @"info.circle";
  }
  return SymbolFor(brouk::GetSiteInfo(browser).level);
}

+ (nullable NSColor*)tintForBrowser:(CefRefPtr<CefBrowser>)browser {
  if (browser &&
      brouk::GetSiteInfo(browser).level == brouk::SecurityLevel::kDangerous) {
    return NSColor.systemRedColor;
  }
  return nil;
}

+ (NSString*)labelForBrowser:(CefRefPtr<CefBrowser>)browser {
  brouk::SecurityLevel level =
      browser ? brouk::GetSiteInfo(browser).level : brouk::SecurityLevel::kNone;
  return [NSString stringWithFormat:@"Site information: %@",
                                    ToNSString(brouk::SecuritySummary(level))];
}

@end
