#import "platform/mac/data_paths.h"

#include <cstdlib>

#include "brouk/app_info.h"

namespace brouk::mac {

namespace {

NSString* EnsureDirectory(NSString* path) {
  // Owner-only access: browsing data includes cookies and sessions.
  [NSFileManager.defaultManager createDirectoryAtPath:path
                          withIntermediateDirectories:YES
                                           attributes:@{
                                             NSFilePosixPermissions : @0700
                                           }
                                                error:nil];
  return path;
}

}  // namespace

NSString* AppDataDirectory() {
  // For developer test runs only: a separate data folder lets a test instance
  // run next to the user's own (CEF allows one instance per data folder).
  const char* dev_dir = std::getenv("BROUK_DEV_DATA_DIR");
  if (dev_dir && dev_dir[0] == '/') {
    return EnsureDirectory(@(dev_dir));
  }
  NSURL* support = [NSFileManager.defaultManager
        URLForDirectory:NSApplicationSupportDirectory
               inDomain:NSUserDomainMask
      appropriateForURL:nil
                 create:YES
                  error:nil];
  NSString* base =
      support.path
          ?: [NSHomeDirectory()
                 stringByAppendingPathComponent:@"Library/Application Support"];
  return EnsureDirectory(
      [base stringByAppendingPathComponent:@(brouk::kAppDataDirName)]);
}

NSString* ProfileDirectory(NSString* profile_id) {
  NSString* profiles =
      [AppDataDirectory() stringByAppendingPathComponent:@"profiles"];
  return EnsureDirectory([profiles stringByAppendingPathComponent:profile_id]);
}

NSString* WebDataRootDirectory() {
  return EnsureDirectory(
      [AppDataDirectory() stringByAppendingPathComponent:@"web"]);
}

NSString* ProfileWebDataDirectory(NSString* profile_id) {
  return EnsureDirectory(
      [WebDataRootDirectory() stringByAppendingPathComponent:profile_id]);
}

}  // namespace brouk::mac
