// The macOS login keychain as the core's SecretStore (008, 009): generic
// password items of one service, readable without asking only by the app
// that created them (macOS asks once after the app's signature changes,
// e.g. after a local rebuild). Thread-safe (the Security framework is).

#import <Foundation/Foundation.h>

#include <memory>

#include "core/secret_store.h"

NS_ASSUME_NONNULL_BEGIN

namespace brouk::mac {

// The keychain service of this app's keys. A developer test run with its
// own data folder (BROUK_DEV_DATA_DIR) gets its own service, so it never
// reads or deletes the user's keys.
NSString* KeychainServiceName();

std::shared_ptr<SecretStore> CreateKeychain(NSString* service);

}  // namespace brouk::mac

NS_ASSUME_NONNULL_END
