// Crash page shown over a tab whose renderer process is gone, like Chrome's
// "Aw, Snap!" page (012). Main thread only.

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface BRSadTabView : NSView

// `errorCode` is shown small below the message (empty: not shown).
- (instancetype)initWithMessage:(NSString*)message
                      errorCode:(NSString*)errorCode
                         reload:(void (^)(void))reload;

@end

NS_ASSUME_NONNULL_END
