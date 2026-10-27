#import "platform/mac/bitwarden_dialogs.h"

#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>

#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

#include "core/crypto.h"

namespace {

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

}  // namespace

// Records the server's trust when it is asked for, then cancels: the
// connection is only for the certificate chain.
@interface BRServerTrustProbe : NSObject <NSURLSessionDelegate>
@property(nonatomic, nullable)
    NSArray* chain;  // SecCertificateRef, leaf first.
@end

@implementation BRServerTrustProbe

- (void)URLSession:(NSURLSession*)session
    didReceiveChallenge:(NSURLAuthenticationChallenge*)challenge
      completionHandler:
          (void (^)(NSURLSessionAuthChallengeDisposition,
                    NSURLCredential* _Nullable))completionHandler {
  SecTrustRef trust = challenge.protectionSpace.serverTrust;
  if ([challenge.protectionSpace.authenticationMethod
          isEqualToString:NSURLAuthenticationMethodServerTrust] &&
      trust != nullptr && SecTrustEvaluateWithError(trust, nullptr)) {
    CFArrayRef chain = SecTrustCopyCertificateChain(trust);
    self.chain = CFBridgingRelease(chain);
  }
  completionHandler(NSURLSessionAuthChallengeCancelAuthenticationChallenge,
                    nil);
}

@end

// The master password sheet. Keeps itself alive until it closes.
@interface BRBitwardenPasswordSheet : NSObject
+ (void)showOn:(nullable NSWindow*)window
       request:(const brouk::BitwardenPasswordRequest&)request
        submit:(brouk::BitwardenPasswordSubmit)submit
     cancelled:(std::function<void()>)cancelled;
@end

@implementation BRBitwardenPasswordSheet {
  NSWindow* _parent;
  NSPanel* _panel;
  NSSecureTextField* _field;
  NSTextField* _problem;
  NSProgressIndicator* _spinner;
  NSTextField* _progress;
  NSButton* _ok;
  NSButton* _cancel;
  brouk::BitwardenPasswordSubmit _submit;
  std::function<void()> _cancelled;
  BRBitwardenPasswordSheet* _keepAlive;
}

+ (void)showOn:(nullable NSWindow*)window
       request:(const brouk::BitwardenPasswordRequest&)request
        submit:(brouk::BitwardenPasswordSubmit)submit
     cancelled:(std::function<void()>)cancelled {
  BRBitwardenPasswordSheet* sheet = [[BRBitwardenPasswordSheet alloc] init];
  [sheet showOn:window
        request:request
         submit:std::move(submit)
      cancelled:std::move(cancelled)];
}

- (NSTextField*)wrappingLabel:(NSString*)text {
  NSTextField* label = [NSTextField wrappingLabelWithString:text];
  label.preferredMaxLayoutWidth = 340;
  return label;
}

- (void)showOn:(nullable NSWindow*)window
       request:(const brouk::BitwardenPasswordRequest&)request
        submit:(brouk::BitwardenPasswordSubmit)submit
     cancelled:(std::function<void()>)cancelled {
  _keepAlive = self;
  _parent = window;
  _submit = std::move(submit);
  _cancelled = std::move(cancelled);

  NSString* titleText;
  NSString* infoText;
  NSString* okTitle;
  if (request.reprompt) {
    titleText = @"Confirm it's you";
    infoText = @"This login is protected in Bitwarden. Enter your master "
               @"password to fill it.";
    okTitle = @"Fill";
  } else {
    titleText = @"Unlock Bitwarden";
    NSString* account = ToNSString(request.account);
    NSString* server = @"";
    if (!request.server.empty()) {
      NSString* host = [NSURL URLWithString:ToNSString(request.server)].host;
      server = [NSString
          stringWithFormat:@" on %@", host ?: ToNSString(request.server)];
    }
    infoText =
        account.length > 0
            ? [NSString stringWithFormat:@"Enter the master password of %@%@.",
                                         account, server]
            : @"Enter your Bitwarden master password.";
    okTitle = @"Unlock";
  }
  NSTextField* title = [NSTextField labelWithString:titleText];
  title.font = [NSFont boldSystemFontOfSize:13];
  NSTextField* info = [self wrappingLabel:infoText];
  _field = [[NSSecureTextField alloc] init];
  _field.placeholderString = @"Master password";
  _field.accessibilityLabel = @"Master password";
  _field.target = self;
  _field.action = @selector(ok:);
  [_field.widthAnchor constraintEqualToConstant:340].active = YES;
  _problem = [self wrappingLabel:@""];
  _problem.textColor = NSColor.systemRedColor;
  _problem.hidden = YES;

  _spinner = [[NSProgressIndicator alloc] init];
  _spinner.style = NSProgressIndicatorStyleSpinning;
  _spinner.controlSize = NSControlSizeSmall;
  _spinner.displayedWhenStopped = NO;
  _progress = [NSTextField
      labelWithString:request.reprompt ? @"Checking…" : @"Unlocking…"];
  _progress.textColor = NSColor.secondaryLabelColor;
  _progress.hidden = YES;
  _cancel = [NSButton buttonWithTitle:@"Cancel"
                               target:self
                               action:@selector(cancel:)];
  _cancel.keyEquivalent = @"\033";
  _ok = [NSButton buttonWithTitle:okTitle target:self action:@selector(ok:)];
  _ok.keyEquivalent = @"\r";
  NSView* spacer = [[NSView alloc] init];
  [spacer setContentHuggingPriority:1
                     forOrientation:NSLayoutConstraintOrientationHorizontal];
  NSStackView* buttons = [NSStackView
      stackViewWithViews:@[ _spinner, _progress, spacer, _cancel, _ok ]];
  buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;

  NSStackView* stack = [NSStackView
      stackViewWithViews:@[ title, info, _field, _problem, buttons ]];
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 10;
  stack.edgeInsets = NSEdgeInsetsMake(20, 20, 16, 20);
  [buttons.widthAnchor constraintEqualToAnchor:_field.widthAnchor].active = YES;

  _panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 380, 160)
                                      styleMask:NSWindowStyleMaskTitled
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
  _panel.title = titleText;
  _panel.contentView = stack;
  _panel.initialFirstResponder = _field;
  if (window) {
    [window beginSheet:_panel completionHandler:nil];
  } else {
    [_panel center];
    [_panel makeKeyAndOrderFront:nil];
  }
}

- (void)setBusy:(BOOL)busy {
  _field.enabled = !busy;
  _ok.enabled = !busy;
  _cancel.enabled = !busy;
  _progress.hidden = !busy;
  if (busy) {
    _problem.hidden = YES;
    [_spinner startAnimation:nil];
  } else {
    [_spinner stopAnimation:nil];
  }
}

- (void)ok:(id)sender {
  if (!_ok.enabled || !_submit) {
    return;
  }
  const char* utf8 = _field.stringValue.UTF8String;
  std::string password = utf8 ? std::string(utf8) : std::string();
  [self setBusy:YES];
  __weak BRBitwardenPasswordSheet* weakSelf = self;
  _submit(std::move(password), [weakSelf](std::optional<std::string> error) {
    BRBitwardenPasswordSheet* sheet = weakSelf;
    if (!sheet) {
      return;
    }
    if (error) {
      [sheet failed:ToNSString(*error)];
    } else {
      [sheet close];
    }
  });
}

- (void)failed:(NSString*)message {
  [self setBusy:NO];
  _problem.stringValue = message;
  _problem.hidden = NO;
  _field.stringValue = @"";
  [_panel makeFirstResponder:_field];
}

- (void)cancel:(id)sender {
  std::function<void()> cancelled = std::move(_cancelled);
  _cancelled = nullptr;
  [self close];
  if (cancelled) {
    cancelled();
  }
}

- (void)close {
  _field.stringValue = @"";
  _submit = nullptr;
  if (_parent && _panel.sheetParent) {
    [_parent endSheet:_panel];
  }
  [_panel orderOut:nil];
  _keepAlive = nil;
}

@end

namespace brouk::mac {

namespace {

std::string Pem(SecCertificateRef certificate) {
  NSData* der = CFBridgingRelease(SecCertificateCopyData(certificate));
  std::string base64 =
      Base64Encode(std::string_view(static_cast<const char*>(der.bytes),
                                    static_cast<std::size_t>(der.length)));
  std::string pem = "-----BEGIN CERTIFICATE-----\n";
  for (std::size_t i = 0; i < base64.size(); i += 64) {
    pem += base64.substr(i, 64) + "\n";
  }
  pem += "-----END CERTIFICATE-----\n";
  return pem;
}

// The server's chain as macOS trusts it, or nil.
NSArray* _Nullable TrustedChain(NSURL* url) {
  BRServerTrustProbe* probe = [[BRServerTrustProbe alloc] init];
  NSURLSessionConfiguration* configuration =
      NSURLSessionConfiguration.ephemeralSessionConfiguration;
  configuration.timeoutIntervalForRequest = 15;
  NSURLSession* session = [NSURLSession sessionWithConfiguration:configuration
                                                        delegate:probe
                                                   delegateQueue:nil];
  NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url];
  request.HTTPMethod = @"HEAD";
  dispatch_semaphore_t finished = dispatch_semaphore_create(0);
  NSURLSessionDataTask* task =
      [session dataTaskWithRequest:request
                 completionHandler:^(NSData*, NSURLResponse*, NSError*) {
                   dispatch_semaphore_signal(finished);
                 }];
  [task resume];
  dispatch_semaphore_wait(finished,
                          dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC));
  [session invalidateAndCancel];
  return probe.chain;
}

}  // namespace

void AskBitwardenPassword(NSWindow* _Nullable window,
                          const BitwardenPasswordRequest& request,
                          BitwardenPasswordSubmit submit,
                          std::function<void()> cancelled) {
  [BRBitwardenPasswordSheet showOn:window
                           request:request
                            submit:std::move(submit)
                         cancelled:std::move(cancelled)];
}

void ConfirmWithTouchId(const std::string& reason, TouchIdDone done) {
  LAContext* context = [[LAContext alloc] init];
  context.localizedFallbackTitle = @"Use Master Password";
  NSError* error = nil;
  if (![context
          canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics
                      error:&error]) {
    done(TouchIdResult::kUnavailable);
    return;
  }
  __block TouchIdDone answer = std::move(done);
  [context evaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics
          localizedReason:ToNSString(reason)
                    reply:^(BOOL success, NSError* _Nullable failure) {
                      TouchIdResult result = TouchIdResult::kPassed;
                      if (!success) {
                        // "Use Master Password" and a locked-out Touch ID
                        // go to the master password; cancelling stops.
                        result = failure.code == LAErrorUserCancel ||
                                         failure.code == LAErrorAppCancel ||
                                         failure.code == LAErrorSystemCancel
                                     ? TouchIdResult::kCancelled
                                     : TouchIdResult::kUnavailable;
                      }
                      dispatch_async(dispatch_get_main_queue(), ^{
                        answer(result);
                      });
                    }];
}

std::filesystem::path PrepareServerCertificates(
    const std::string& server_url,
    const std::filesystem::path& folder) {
  // Servers already prepared in this run; `mutex` guards `prepared`.
  static std::mutex mutex;
  static std::map<std::string, std::filesystem::path> prepared;
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = prepared.find(server_url);
    if (it != prepared.end()) {
      return it->second;
    }
  }
  NSURL* url = [NSURL URLWithString:ToNSString(server_url)];
  if (!url || ![url.scheme isEqualToString:@"https"]) {
    return {};
  }
  NSArray* chain = TrustedChain(url);
  std::filesystem::path path = folder / "bitwarden-certificates.pem";
  if (chain.count == 0) {
    // Not reachable now: an earlier copy still helps.
    std::error_code error;
    return std::filesystem::exists(path, error) ? path
                                                : std::filesystem::path();
  }
  // The authorities above the server's own certificate; a single
  // certificate (self-signed, trusted by the user) goes in itself.
  std::string pem;
  for (NSUInteger i = chain.count == 1 ? 0 : 1; i < chain.count; ++i) {
    pem += Pem((__bridge SecCertificateRef)chain[i]);
  }
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << pem;
    if (!file) {
      return {};
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex);
  prepared[server_url] = path;
  return path;
}

}  // namespace brouk::mac
