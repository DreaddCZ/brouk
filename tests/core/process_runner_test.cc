#include "core/process_runner.h"

#include <gtest/gtest.h>

namespace brouk {
namespace {

ProcessRequest Shell(std::string script) {
  ProcessRequest request;
  request.program = "/bin/sh";
  request.arguments = {"-c", std::move(script)};
  return request;
}

TEST(ProcessRunnerTest, PassesInputAndReadsBothOutputs) {
  ProcessRequest request = Shell("cat; echo problem >&2; exit 3");
  request.input = "secret input";
  Result<ProcessOutput> output = RunProcess(request);
  ASSERT_TRUE(output);
  EXPECT_EQ(output.value().exit_code, 3);
  EXPECT_FALSE(output.value().timed_out);
  EXPECT_EQ(output.value().out, "secret input");
  EXPECT_EQ(output.value().err, "problem\n");
}

TEST(ProcessRunnerTest, LargeInputAndOutput) {
  // More than a pipe's buffer in both directions at once.
  ProcessRequest request = Shell("cat");
  request.input = std::string(1 << 20, 'x');
  Result<ProcessOutput> output = RunProcess(request);
  ASSERT_TRUE(output);
  EXPECT_EQ(output.value().out.size(), request.input.size());
}

TEST(ProcessRunnerTest, SetsAndRemovesEnvironment) {
  setenv("BROUK_TEST_INHERITED", "inherited", 1);
  setenv("BROUK_TEST_REMOVED", "removed", 1);
  ProcessRequest request = Shell(
      "printf '%s|%s|%s' \"$BROUK_TEST_INHERITED\" \"$BROUK_TEST_REMOVED\" "
      "\"$BROUK_TEST_SET\"");
  request.environment = {{"BROUK_TEST_SET", "set"}};
  request.remove_environment = {"BROUK_TEST_REMOVED"};
  Result<ProcessOutput> output = RunProcess(request);
  ASSERT_TRUE(output);
  EXPECT_EQ(output.value().out, "inherited||set");
}

TEST(ProcessRunnerTest, ProgramThatStopsReadingDoesNotKillUs) {
  ProcessRequest request = Shell("exit 0");
  request.input = std::string(1 << 20, 'x');
  Result<ProcessOutput> output = RunProcess(request);
  ASSERT_TRUE(output);
  EXPECT_EQ(output.value().exit_code, 0);
}

TEST(ProcessRunnerTest, KillsAfterTheTimeout) {
  ProcessRequest request = Shell("sleep 30");
  request.timeout = std::chrono::milliseconds(200);
  Result<ProcessOutput> output = RunProcess(request);
  ASSERT_TRUE(output);
  EXPECT_TRUE(output.value().timed_out);
  EXPECT_EQ(output.value().exit_code, -1);
}

TEST(ProcessRunnerTest, MissingProgramIsAnError) {
  ProcessRequest request;
  request.program = "/nonexistent/bw";
  EXPECT_FALSE(RunProcess(request));
}

}  // namespace
}  // namespace brouk
