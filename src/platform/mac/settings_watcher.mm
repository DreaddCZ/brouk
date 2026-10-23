#import "platform/mac/settings_watcher.h"

#import <CoreServices/CoreServices.h>

@interface BRSettingsWatcher ()
- (void)handlePaths:(NSArray<NSString*>*)paths;
@end

namespace {

void StreamCallback(ConstFSEventStreamRef /*stream*/,
                    void* info,
                    size_t count,
                    void* paths,
                    const FSEventStreamEventFlags* /*flags*/,
                    const FSEventStreamEventId* /*ids*/) {
  // kFSEventStreamCreateFlagUseCFTypes: `paths` is a CFArray of CFStrings.
  NSArray<NSString*>* list = (__bridge NSArray<NSString*>*)paths;
  if (count > 0) {
    [(__bridge BRSettingsWatcher*)info handlePaths:list];
  }
}

}  // namespace

@implementation BRSettingsWatcher {
  NSString* _directory;
  NSString* _ignored;  // Chromium's data folder.
  void (^_changed)(void);
  FSEventStreamRef _stream;
  BOOL _scheduled;
}

- (instancetype)initWithDirectory:(NSString*)directory
                          changed:(void (^)(void))changed {
  if ((self = [super init])) {
    _directory = directory.stringByResolvingSymlinksInPath;
    _ignored = [_directory stringByAppendingString:@"/web/"];
    _changed = changed;
    FSEventStreamContext context = {};
    // The stream does not retain us; it is stopped in -dealloc.
    context.info = (__bridge void*)self;
    _stream = FSEventStreamCreate(kCFAllocatorDefault, StreamCallback, &context,
                                  (__bridge CFArrayRef) @[ _directory ],
                                  kFSEventStreamEventIdSinceNow,
                                  /*latency=*/0.2,
                                  kFSEventStreamCreateFlagFileEvents |
                                      kFSEventStreamCreateFlagUseCFTypes |
                                      kFSEventStreamCreateFlagNoDefer);
    if (_stream) {
      FSEventStreamSetDispatchQueue(_stream, dispatch_get_main_queue());
      FSEventStreamStart(_stream);
    }
  }
  return self;
}

- (void)dealloc {
  if (_stream) {
    FSEventStreamStop(_stream);
    FSEventStreamInvalidate(_stream);
    FSEventStreamRelease(_stream);
  }
}

// Main thread (the stream's queue).
- (void)handlePaths:(NSArray<NSString*>*)paths {
  BOOL relevant = NO;
  for (NSString* path in paths) {
    NSString* resolved = path.stringByResolvingSymlinksInPath;
    if (![resolved hasPrefix:_ignored] &&
        ([resolved.lastPathComponent hasPrefix:@"settings.yaml"] ||
         [resolved.lastPathComponent hasPrefix:@"request-rules.yaml"])) {
      relevant = YES;
      break;
    }
  }
  if (!relevant || _scheduled) {
    return;
  }
  // Editors often write a file in several steps; read it once they are done.
  _scheduled = YES;
  __weak BRSettingsWatcher* weakSelf = self;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC),
                 dispatch_get_main_queue(), ^{
                   BRSettingsWatcher* strongSelf = weakSelf;
                   if (strongSelf) {
                     strongSelf->_scheduled = NO;
                     strongSelf->_changed();
                   }
                 });
}

@end
