// Asks for the password protecting the passwords in a profile export (008):
// a new one (typed twice) when exporting, the existing one when importing.
// Main thread only.

#import <Cocoa/Cocoa.h>

#include <functional>
#include <optional>
#include <string>

NS_ASSUME_NONNULL_BEGIN

namespace brouk::mac {

// What the user chose: a password, going on without the passwords, or
// cancelling the whole export or import.
struct ExportPasswordAnswer {
  enum class Choice { kPassword, kWithoutPasswords, kCancel };
  Choice choice = Choice::kCancel;
  std::string password;
};
using ExportPasswordDone = std::function<void(ExportPasswordAnswer)>;

// `confirm`: a new password, typed twice (export). `message` explains the
// situation, e.g. the previous attempt's error. As a sheet on `window` when
// given.
void AskExportPassword(NSWindow* _Nullable window,
                       bool confirm,
                       NSString* message,
                       ExportPasswordDone done);

}  // namespace brouk::mac

NS_ASSUME_NONNULL_END
