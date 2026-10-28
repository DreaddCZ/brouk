// Test cases for brouk.yml in Objective-C++ (see brouk.ts).

void Cases(NSString* token, NSString* name) {
  // ruleid: log-secret
  NSLog(@"Bitwarden: %@", token);
  // ok: log-secret
  NSLog(@"Profile: %@", name);
  // ruleid: cpp-process-spawn
  NSTask* task = [[NSTask alloc] init];
}
