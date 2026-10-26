// The link picker (010): one dialog that chooses the profile (when a rule
// lists several) and then the window (when the profile's group is in
// several windows) for a link. It only shows choices and reports the
// answer; brouk::DecideProfile and brouk::DecideWindow decide when it is
// needed. Number keys choose at once, arrows plus Return also work, Escape
// cancels (or goes back from the window step). Main thread only.

#import <Cocoa/Cocoa.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/profile_id.h"

NS_ASSUME_NONNULL_BEGIN

// A profile the picker offers.
@interface BRLinkPickerProfile : NSObject
@property(nonatomic) brouk::ProfileId profileId;
@property(nonatomic, copy) NSString* name;
@property(nonatomic) std::string color;
@end

// One profile group of a window, as a chip ("Work 5").
@interface BRLinkPickerGroup : NSObject
@property(nonatomic) brouk::ProfileId profileId;
@property(nonatomic, copy) NSString* name;
@property(nonatomic) std::string color;
@property(nonatomic) NSUInteger tabCount;
@end

// A window the picker offers.
@interface BRLinkPickerWindow : NSObject
@property(nonatomic) std::int64_t windowId;
@property(nonatomic, copy) NSString* title;  // Of its active tab.
@property(nonatomic, nullable) NSImage* favicon;
// Of its active tab; until it arrives (or if there is none) the favicon is
// shown.
@property(nonatomic, nullable) NSImage* thumbnail;
@property(nonatomic, copy) NSArray<BRLinkPickerGroup*>* groups;
@end

// What the user chose.
struct BRLinkPickerResult {
  bool cancelled = false;
  brouk::ProfileId profile;
  // "Always open links matching … in this profile" was checked.
  bool always = false;
  enum class Window {
    kNotAsked,   // No window step: routing decides.
    kExisting,   // `window_id`.
    kNewWindow,  // "New window".
  };
  Window window = Window::kNotAsked;
  std::int64_t window_id = 0;
};

@interface BRLinkPicker : NSObject

// Shows the picker centred on `screen`, in front of all windows.
//   `profiles`: more than one shows the profile step first; one skips it.
//   `rulePattern`: for the "Always open…" checkbox (nil: no checkbox).
//   `windowsForProfile`: the windows to offer for the chosen profile, most
//   recently used first; nil or empty skips the window step.
//   `done`: called once, after the picker has closed.
- (void)showURL:(NSString*)url
                  via:(nullable NSString*)wrapper
                 from:(NSString*)source
             profiles:(NSArray<BRLinkPickerProfile*>*)profiles
          rulePattern:(nullable NSString*)rulePattern
    windowsForProfile:(NSArray<BRLinkPickerWindow*>* _Nullable (^)(
                          const brouk::ProfileId& profile))windowsForProfile
             onScreen:(nullable NSScreen*)screen
                 done:(void (^)(const BRLinkPickerResult& result))done;

// A thumbnail arrived after the window step was shown.
- (void)setThumbnail:(NSImage*)thumbnail forWindow:(std::int64_t)windowId;

// Closes the picker as if the user cancelled.
- (void)cancel;

@property(nonatomic, readonly, getter=isVisible) BOOL visible;

@end

NS_ASSUME_NONNULL_END
