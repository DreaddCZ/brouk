#import "platform/mac/keychain.h"

#import <Security/Security.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "brouk/app_info.h"

namespace brouk::mac {

namespace {

NSString* ToNSString(std::string_view text) {
  return [[NSString alloc] initWithBytes:text.data()
                                  length:text.size()
                                encoding:NSUTF8StringEncoding]
             ?: @"";
}

Error KeychainError(NSString* what, OSStatus status) {
  NSString* message =
      CFBridgingRelease(SecCopyErrorMessageString(status, nullptr));
  return Error{std::string([what UTF8String]) + ": " +
               std::string((message ?: @"unknown error").UTF8String) + " (" +
               std::to_string(status) + ")"};
}

class Keychain : public SecretStore {
 public:
  explicit Keychain(NSString* service) : service_([service copy]) {}

  Result<std::optional<std::string>> Read(std::string_view account) override {
    NSDictionary* query = @{
      (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
      (__bridge id)kSecAttrService : service_,
      (__bridge id)kSecAttrAccount : ToNSString(account),
      (__bridge id)kSecReturnData : @YES,
      (__bridge id)kSecMatchLimit : (__bridge id)kSecMatchLimitOne,
    };
    CFTypeRef result = nullptr;
    OSStatus status =
        SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
    if (status == errSecItemNotFound) {
      return std::optional<std::string>();
    }
    if (status != errSecSuccess) {
      return KeychainError(@"Reading the key failed", status);
    }
    NSData* data = CFBridgingRelease(result);
    return std::optional<std::string>(
        std::string(static_cast<const char*>(data.bytes), data.length));
  }

  std::optional<Error> Write(std::string_view account,
                             std::string_view secret) override {
    NSData* data = [NSData dataWithBytes:secret.data() length:secret.size()];
    NSDictionary* query = @{
      (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
      (__bridge id)kSecAttrService : service_,
      (__bridge id)kSecAttrAccount : ToNSString(account),
    };
    OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)query,
                                    (__bridge CFDictionaryRef)
                                        @{(__bridge id)kSecValueData : data});
    if (status == errSecItemNotFound) {
      NSMutableDictionary* item = [query mutableCopy];
      item[(__bridge id)kSecValueData] = data;
      item[(__bridge id)kSecAttrLabel] = service_;
      status = SecItemAdd((__bridge CFDictionaryRef)item, nullptr);
    }
    if (status != errSecSuccess) {
      return KeychainError(@"Saving the key failed", status);
    }
    return std::nullopt;
  }

  std::optional<Error> Remove(std::string_view account) override {
    NSDictionary* query = @{
      (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
      (__bridge id)kSecAttrService : service_,
      (__bridge id)kSecAttrAccount : ToNSString(account),
    };
    OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
    if (status != errSecSuccess && status != errSecItemNotFound) {
      return KeychainError(@"Removing the key failed", status);
    }
    return std::nullopt;
  }

 private:
  NSString* service_;
};

}  // namespace

NSString* KeychainServiceName() {
  NSString* service = [NSString stringWithFormat:@"%s Keys", kAppName];
  const char* dev_dir = std::getenv("BROUK_DEV_DATA_DIR");
  if (dev_dir && dev_dir[0] == '/') {
    service = [service stringByAppendingFormat:@" (%s)", dev_dir];
  }
  return service;
}

std::shared_ptr<SecretStore> CreateKeychain(NSString* service) {
  return std::make_shared<Keychain>(service);
}

}  // namespace brouk::mac
