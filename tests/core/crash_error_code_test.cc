#include "core/crash_error_code.h"

#include <csignal>

#include <gtest/gtest.h>

namespace brouk {
namespace {

TEST(CrashErrorCodeTest, NamesCommonSignals) {
  EXPECT_EQ(CrashErrorCodeText(SIGSEGV), "Error code: SIGSEGV");
  EXPECT_EQ(CrashErrorCodeText(SIGABRT), "Error code: SIGABRT");
  EXPECT_EQ(CrashErrorCodeText(SIGKILL), "Error code: SIGKILL");
}

TEST(CrashErrorCodeTest, UnknownCodesAreNumbers) {
  EXPECT_EQ(CrashErrorCodeText(1234), "Error code: 1234");
}

TEST(CrashErrorCodeTest, ZeroMeansNoCode) {
  EXPECT_EQ(CrashErrorCodeText(0), "");
}

}  // namespace
}  // namespace brouk
