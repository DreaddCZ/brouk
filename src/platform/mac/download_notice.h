// Minimal download notice at the bottom of the window (012, item 18, until
// the downloads spec 014): shows progress, and when finished the file name
// with "Show in Finder". Main thread only.

#import <Cocoa/Cocoa.h>

#include "cef/tab_client.h"

NS_ASSUME_NONNULL_BEGIN

@interface BRDownloadNotice : NSView

// Shows the latest state of a download (the most recent download wins).
- (void)updateWithInfo:(const brouk::DownloadInfo&)info;

@end

NS_ASSUME_NONNULL_END
