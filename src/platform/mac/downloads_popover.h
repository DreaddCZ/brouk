// Downloads in the toolbar (014): the button (shown from the first download
// of the run, with a progress ring while anything downloads and a dot for
// a finished download not looked at yet) and its popup listing the last 24
// hours of downloads of all profiles. Main thread only.

#import <Cocoa/Cocoa.h>

#include <string>
#include <vector>

#include "cef/download_service.h"

NS_ASSUME_NONNULL_BEGIN

@interface BRDownloadsButton : NSButton
// 0..1, or -1 for "downloading, size unknown"; nil-like NAN hides the ring.
@property(nonatomic) double progress;
@property(nonatomic) BOOL showsDot;
@end

// What the popup does with a download; implemented by the window controller.
@protocol BRDownloadsActions <NSObject>
- (NSColor*)colorOfProfile:(const brouk::ProfileId&)profile;
- (void)retryDownload:(const brouk::DownloadEntry&)entry;
- (void)showAllDownloads;
- (brouk::DownloadService*)downloadService;
@end

@interface BRDownloadsPopover : NSObject
+ (void)showEntries:(std::vector<brouk::DownloadEntry>)entries
         relativeTo:(NSView*)anchor
            actions:(id<BRDownloadsActions>)actions;
// Refreshes an open popup with new states.
+ (void)updateEntries:(std::vector<brouk::DownloadEntry>)entries;
+ (BOOL)isShown;
@end

// Shared with the downloads page: file actions on a finished download.
namespace brouk::mac {
void OpenDownloadedFile(const std::string& path);
void RevealDownloadedFile(const std::string& path);
// Moves the file to the Trash (as Chrome's "Delete" does on macOS).
bool TrashDownloadedFile(const std::string& path);
bool DownloadedFileExists(const std::string& path);
}  // namespace brouk::mac

NS_ASSUME_NONNULL_END
