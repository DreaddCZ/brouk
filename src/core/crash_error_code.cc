#include "core/crash_error_code.h"

#include <csignal>
#include <string>

namespace brouk {

namespace {

const char* SignalName(int code) {
  switch (code) {
    case SIGILL:
      return "SIGILL";
#if !defined(_WIN32)
    // POSIX only. Windows reports crashes as exception codes instead
    // (STATUS_ACCESS_VIOLATION, ...), named when the Windows app is built.
    case SIGTRAP:
      return "SIGTRAP";
    case SIGBUS:
      return "SIGBUS";
    case SIGKILL:
      return "SIGKILL";
#endif
    case SIGABRT:
      return "SIGABRT";
    case SIGFPE:
      return "SIGFPE";
    case SIGSEGV:
      return "SIGSEGV";
    case SIGTERM:
      return "SIGTERM";
    default:
      return nullptr;
  }
}

}  // namespace

std::string CrashErrorCodeText(int code) {
  if (code == 0) {
    return {};
  }
  const char* name = SignalName(code);
  return "Error code: " + (name ? std::string(name) : std::to_string(code));
}

}  // namespace brouk
