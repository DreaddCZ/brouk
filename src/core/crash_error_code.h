#ifndef BROUK_CORE_CRASH_ERROR_CODE_H_
#define BROUK_CORE_CRASH_ERROR_CODE_H_

#include <string>

namespace brouk {

// Text for the crash page's error code, like Chrome's "Error code: SIGSEGV".
// On POSIX the code is the signal that ended the renderer; unknown codes are
// shown as numbers. Returns an empty string for 0 (no code).
std::string CrashErrorCodeText(int code);

}  // namespace brouk

#endif  // BROUK_CORE_CRASH_ERROR_CODE_H_
