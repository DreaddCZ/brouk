#include "core/crash_error_code.h"

#include <csignal>
#include <cstdint>
#include <string>
#include <string_view>

namespace brouk {

namespace {

#if !defined(_WIN32)
// POSIX: the signal that ended the renderer.
const char* SignalName(int code) {
  switch (code) {
    case SIGILL:
      return "SIGILL";
    case SIGTRAP:
      return "SIGTRAP";
    case SIGBUS:
      return "SIGBUS";
    case SIGKILL:
      return "SIGKILL";
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
#endif

// Windows exception codes (NTSTATUS) a crashed renderer ends with.
const char* WindowsStatusName(std::uint32_t code) {
  switch (code) {
    case 0x80000003:
      return "STATUS_BREAKPOINT";
    case 0xC0000005:
      return "STATUS_ACCESS_VIOLATION";
    case 0xC000001D:
      return "STATUS_ILLEGAL_INSTRUCTION";
    case 0xC0000094:
      return "STATUS_INTEGER_DIVIDE_BY_ZERO";
    case 0xC00000FD:
      return "STATUS_STACK_OVERFLOW";
    case 0xC0000374:
      return "STATUS_HEAP_CORRUPTION";
    case 0xC0000409:
      return "STATUS_STACK_BUFFER_OVERRUN";
    default:
      return nullptr;
  }
}

}  // namespace

std::string CrashErrorCodeText(int code) {
#if defined(_WIN32)
  return WindowsCrashErrorCodeText(code);
#else
  if (code == 0) {
    return {};
  }
  const char* name = SignalName(code);
  return "Error code: " + (name ? std::string(name) : std::to_string(code));
#endif
}

std::string WindowsCrashErrorCodeText(int code) {
  if (code == 0) {
    return {};
  }
  const auto status = static_cast<std::uint32_t>(code);
  if (const char* name = WindowsStatusName(status)) {
    return std::string("Error code: ") + name;
  }
  constexpr std::string_view kHex = "0123456789ABCDEF";
  std::string text = "Error code: 0x";
  for (int shift = 28; shift >= 0; shift -= 4) {
    text.push_back(kHex[(status >> shift) & 0xF]);
  }
  return text;
}

}  // namespace brouk
