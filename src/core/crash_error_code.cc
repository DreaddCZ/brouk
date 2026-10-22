#include "core/crash_error_code.h"

#include <csignal>
#include <string>

namespace brouk {

namespace {

const char* SignalName(int code) {
  switch (code) {
    case SIGILL:
      return "SIGILL";
    case SIGTRAP:
      return "SIGTRAP";
    case SIGABRT:
      return "SIGABRT";
    case SIGBUS:
      return "SIGBUS";
    case SIGFPE:
      return "SIGFPE";
    case SIGKILL:
      return "SIGKILL";
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
