// Where the app keeps its data on macOS (002, 004), under
// ~/Library/Application Support/<APP_DATA_DIR_NAME>/:
//   profiles/<id>/   our own per-profile files (settings.yaml, 004)
//   web/             Chromium's data root (CefSettings.root_cache_path)
//   web/<id>/        a profile's web data: cookies, cache, storage (005)
// Chromium requires profile folders to be direct children of its root and
// fills the root with install-wide files, hence the separate "web" folder.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

namespace brouk::mac {

// Creates the directory if needed. The environment variable
// BROUK_DEV_DATA_DIR (an absolute path) replaces it for developer test runs.
NSString* AppDataDirectory();
// `profile_id` is a stable id (never the display name, see 005).
NSString* ProfileDirectory(NSString* profile_id);
// Chromium's data root.
NSString* WebDataRootDirectory();
// A profile's Chromium data (cookies, cache, storage).
NSString* ProfileWebDataDirectory(NSString* profile_id);

}  // namespace brouk::mac

NS_ASSUME_NONNULL_END
