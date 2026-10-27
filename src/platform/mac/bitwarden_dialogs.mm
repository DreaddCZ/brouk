#import "platform/mac/bitwarden_dialogs.h"

#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>

#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

#include "core/crypto.h"

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

namespace brouk::mac {

namespace {

NSString* ToNSString(const std::string& s) {
  return [[NSString alloc] initWithBytes:s.data()
                                  length:s.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

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
                          BitwardenPasswordDone done) {
  NSAlert* alert = [[NSAlert alloc] init];
  NSString* account = ToNSString(request.account);
  if (request.reprompt) {
    alert.messageText = @"Confirm it's you";
    alert.informativeText =
        @"This login is protected in Bitwarden. Enter your master password "
        @"to fill it.";
    [alert addButtonWithTitle:@"Fill"];
  } else {
    alert.messageText = @"Unlock Bitwarden";
    NSString* server =
        request.server.empty()
            ? @""
            : [NSString
                  stringWithFormat:@" on %@",
                                   [NSURL
                                       URLWithString:ToNSString(request.server)]
                                           .host
                                       ?: ToNSString(request.server)];
    alert.informativeText =
        account.length > 0
            ? [NSString stringWithFormat:@"Enter the master password of %@%@.",
                                         account, server]
            : @"Enter your Bitwarden master password.";
    [alert addButtonWithTitle:@"Unlock"];
  }
  if (!request.error.empty()) {
    alert.informativeText = [alert.informativeText
        stringByAppendingFormat:@"\n\n%@", ToNSString(request.error)];
  }
  [alert addButtonWithTitle:@"Cancel"];
  NSSecureTextField* field =
      [[NSSecureTextField alloc] initWithFrame:NSMakeRect(0, 0, 260, 24)];
  field.placeholderString = @"Master password";
  field.accessibilityLabel = @"Master password";
  alert.accessoryView = field;
  alert.window.initialFirstResponder = field;

  __block BitwardenPasswordDone answer = std::move(done);
  void (^finish)(NSModalResponse) = ^(NSModalResponse response) {
    std::optional<std::string> password;
    if (response == NSAlertFirstButtonReturn) {
      const char* utf8 = field.stringValue.UTF8String;
      password = utf8 ? std::string(utf8) : std::string();
    }
    field.stringValue = @"";
    answer(std::move(password));
  };
  if (window) {
    [alert beginSheetModalForWindow:window completionHandler:finish];
  } else {
    finish([alert runModal]);
  }
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
