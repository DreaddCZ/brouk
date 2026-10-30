#ifndef BROUK_CORE_CRASH_ERROR_CODE_H_
#define BROUK_CORE_CRASH_ERROR_CODE_H_

#include <string>

namespace brouk {

// Text for the crash page's error code, like Chrome's "Error code: SIGSEGV".
// On POSIX the code is the signal that ended the renderer; unknown codes are
// shown as numbers. On Windows it is the process's exit code
// (WindowsCrashErrorCodeText). Returns an empty string for 0 (no code).
std::string CrashErrorCodeText(int code);

// The Windows form, here on every platform for the tests: the exception
// that ended the renderer by name, as Chrome shows it
// ("STATUS_ACCESS_VIOLATION"); unknown codes in hexadecimal ("0xC0000017").
std::string WindowsCrashErrorCodeText(int code);

}  // namespace brouk

#endif  // BROUK_CORE_CRASH_ERROR_CODE_H_
