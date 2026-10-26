#import "platform/mac/favicon_cache.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "cef/history_service.h"

NSNotificationName const BRFaviconsDidChangeNotification =
    @"BRFaviconsDidChangeNotification";

namespace {

using Key = std::pair<brouk::ProfileId, std::string>;

struct Entry {
  NSImage* __strong image = nil;  // nil: none, or not known yet.
  std::vector<std::uint8_t> png;
  bool loading = false;
};

std::string HostOf(const std::string& url) {
  NSURL* parsed = [NSURL URLWithString:@(url.c_str()) ?: @""];
  return parsed.host.lowercaseString.UTF8String ?: "";
}

}  // namespace

@implementation BRFaviconCache {
  brouk::HistoryService* _history;  // Not owned; null once detached.
  std::map<Key, Entry> _entries;
  // URLs to fetch per profile, sent together on the next turn of the loop.
  std::map<brouk::ProfileId, std::vector<std::string>> _pending;
  BOOL _flushScheduled;
}

- (instancetype)initWithHistory:(brouk::HistoryService*)history {
  if ((self = [super init])) {
    _history = history;
  }
  return self;
}

- (void)detach {
  _history = nullptr;
  _pending.clear();
}

- (nullable NSImage*)iconForURL:(const std::string&)url
                        profile:(const brouk::ProfileId&)profile {
  if (url.empty()) {
    return nil;
  }
  auto [it, added] = _entries.try_emplace(Key{profile, url});
  if (added) {
    [self fetch:url profile:profile];
  }
  return it->second.image;
}

- (void)iconChangedForURL:(const std::string&)url
                  profile:(const brouk::ProfileId&)profile {
  std::string host = HostOf(url);
  for (auto& [key, entry] : _entries) {
    if (key.first != profile || entry.loading) {
      continue;
    }
    // A page without an icon of its own shows its site's.
    if (key.second == url || (!entry.image && HostOf(key.second) == host)) {
      [self fetch:key.second profile:profile];
    }
  }
}

- (void)fetch:(const std::string&)url profile:(const brouk::ProfileId&)profile {
  if (!_history) {
    return;
  }
  _entries[Key{profile, url}].loading = true;
  _pending[profile].push_back(url);
  if (_flushScheduled) {
    return;
  }
  _flushScheduled = YES;
  __weak BRFaviconCache* weakSelf = self;
  dispatch_async(dispatch_get_main_queue(), ^{
    [weakSelf flush];
  });
}

- (void)flush {
  _flushScheduled = NO;
  std::map<brouk::ProfileId, std::vector<std::string>> pending;
  pending.swap(_pending);
  if (!_history) {
    return;
  }
  __weak BRFaviconCache* weakSelf = self;
  for (auto& [profile, urls] : pending) {
    brouk::ProfileId target = profile;
    std::vector<std::string> asked = urls;
    _history->Favicons(
        profile, std::move(urls),
        [weakSelf, target, asked = std::move(asked)](
            std::map<std::string, std::vector<std::uint8_t>> icons) {
          [weakSelf received:icons asked:asked profile:target];
        });
  }
}

- (void)received:(const std::map<std::string, std::vector<std::uint8_t>>&)icons
           asked:(const std::vector<std::string>&)asked
         profile:(const brouk::ProfileId&)profile {
  BOOL changed = NO;
  for (const std::string& url : asked) {
    Entry& entry = _entries[Key{profile, url}];
    entry.loading = false;
    auto icon = icons.find(url);
    if (icon == icons.end()) {
      changed = changed || entry.image != nil;
      entry.image = nil;
      entry.png.clear();
      continue;
    }
    if (icon->second == entry.png) {
      continue;
    }
    NSData* data = [NSData dataWithBytes:icon->second.data()
                                  length:icon->second.size()];
    NSImage* image = [[NSImage alloc] initWithData:data];
    // The PNG is at 2x; drawn at 16 pt.
    image.size = NSMakeSize(16, 16);
    entry.image = image;
    entry.png = icon->second;
    changed = YES;
  }
  if (changed) {
    [NSNotificationCenter.defaultCenter
        postNotificationName:BRFaviconsDidChangeNotification
                      object:self];
  }
}

@end
