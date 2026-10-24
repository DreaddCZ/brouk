// Shown over a tab whose page load the ad blocker blocked (007): which list
// blocked it, with "Allow this site" and "Go back". Main thread only.

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface BRAdblockBlockedView : NSView

- (instancetype)initWithHost:(NSString*)host
                    listName:(NSString*)listName
                        rule:(NSString*)rule
                       allow:(void (^)(void))allow
                      goBack:(void (^)(void))goBack;

@end

NS_ASSUME_NONNULL_END
