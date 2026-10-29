#include "core/crash_error_code.h"

#include <csignal>

#include <gtest/gtest.h>

namespace brouk {
namespace {

#if !defined(_WIN32)
TEST(CrashErrorCodeTest, NamesCommonSignals) {
  EXPECT_EQ(CrashErrorCodeText(SIGSEGV), "Error code: SIGSEGV");
  EXPECT_EQ(CrashErrorCodeText(SIGABRT), "Error code: SIGABRT");
  EXPECT_EQ(CrashErrorCodeText(SIGKILL), "Error code: SIGKILL");
}

TEST(CrashErrorCodeTest, UnknownCodesAreNumbers) {
  EXPECT_EQ(CrashErrorCodeText(1234), "Error code: 1234");
}
#endif

// Windows exit codes come as negative ints (0x80000003 is -2147483645).
TEST(CrashErrorCodeTest, NamesWindowsExceptions) {
  EXPECT_EQ(WindowsCrashErrorCodeText(-2147483645),
            "Error code: STATUS_BREAKPOINT");
  EXPECT_EQ(WindowsCrashErrorCodeText(static_cast<int>(0xC0000005u)),
            "Error code: STATUS_ACCESS_VIOLATION");
  EXPECT_EQ(WindowsCrashErrorCodeText(static_cast<int>(0xC0000409u)),
            "Error code: STATUS_STACK_BUFFER_OVERRUN");
}

TEST(CrashErrorCodeTest, UnknownWindowsCodesAreHexadecimal) {
  EXPECT_EQ(WindowsCrashErrorCodeText(static_cast<int>(0xC0000017u)),
            "Error code: 0xC0000017");
  EXPECT_EQ(WindowsCrashErrorCodeText(1), "Error code: 0x00000001");
  EXPECT_EQ(WindowsCrashErrorCodeText(0), "");
}

TEST(CrashErrorCodeTest, ZeroMeansNoCode) {
  EXPECT_EQ(CrashErrorCodeText(0), "");
}

}  // namespace
}  // namespace brouk
