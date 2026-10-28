#include "core/process_runner.h"

#include <gtest/gtest.h>

#include <cstdlib>

namespace brouk {
namespace {

// process_runner_helper (see its commands), built next to the tests.
ProcessRequest Helper(std::vector<std::string> commands) {
  ProcessRequest request;
  request.program = BROUK_PROCESS_RUNNER_HELPER;
  request.arguments = std::move(commands);
  return request;
}

void SetVariable(const char* name, const char* value) {
#if defined(_WIN32)
  _putenv_s(name, value);
#else
  setenv(name, value, 1);
#endif
}

TEST(ProcessRunnerTest, PassesInputAndReadsBothOutputs) {
  ProcessRequest request = Helper({"cat", "err", "problem", "exit", "3"});
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
  ProcessRequest request = Helper({"cat"});
  request.input = std::string(1 << 20, 'x');
  Result<ProcessOutput> output = RunProcess(request);
  ASSERT_TRUE(output);
  EXPECT_EQ(output.value().out.size(), request.input.size());
}

TEST(ProcessRunnerTest, PassesArgumentsUnchanged) {
  // Arguments that a command line (Windows) must quote, and that a shell
  // would interpret: none of them may be changed or split.
  std::vector<std::string> arguments = {"plain",
                                        "two words",
                                        "",
                                        "quote\"inside",
                                        "back\\slash",
                                        "trailing\\",
                                        "trailing space\\",
                                        "\\\"both\\\"",
                                        "$HOME %PATH% `x` & | > ;"};
  std::vector<std::string> commands = {"args"};
  commands.insert(commands.end(), arguments.begin(), arguments.end());
  Result<ProcessOutput> output = RunProcess(Helper(commands));
  ASSERT_TRUE(output);
  std::string expected;
  for (const std::string& argument : arguments) {
    expected += argument + "\n";
  }
  EXPECT_EQ(output.value().out, expected);
}

TEST(ProcessRunnerTest, SetsAndRemovesEnvironment) {
  SetVariable("BROUK_TEST_INHERITED", "inherited");
  SetVariable("BROUK_TEST_REMOVED", "removed");
  ProcessRequest request =
      Helper({"env", "BROUK_TEST_INHERITED", "env", "BROUK_TEST_REMOVED", "env",
              "BROUK_TEST_SET"});
  request.environment = {{"BROUK_TEST_SET", "set"}};
  request.remove_environment = {"BROUK_TEST_REMOVED"};
  Result<ProcessOutput> output = RunProcess(request);
  ASSERT_TRUE(output);
  EXPECT_EQ(output.value().out, "inherited||set|");
}

TEST(ProcessRunnerTest, ProgramThatStopsReadingDoesNotKillUs) {
  ProcessRequest request = Helper({"exit", "0"});
  request.input = std::string(1 << 20, 'x');
  Result<ProcessOutput> output = RunProcess(request);
  ASSERT_TRUE(output);
  EXPECT_EQ(output.value().exit_code, 0);
}

TEST(ProcessRunnerTest, KillsAfterTheTimeout) {
  ProcessRequest request = Helper({"sleep", "30000"});
  request.timeout = std::chrono::milliseconds(200);
  auto start = std::chrono::steady_clock::now();
  Result<ProcessOutput> output = RunProcess(request);
  ASSERT_TRUE(output);
  EXPECT_TRUE(output.value().timed_out);
  EXPECT_EQ(output.value().exit_code, -1);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
}

TEST(ProcessRunnerTest, MissingProgramIsAnError) {
  ProcessRequest request;
  request.program = "/nonexistent/bw";
  EXPECT_FALSE(RunProcess(request));
}

#if defined(_WIN32)
TEST(ProcessRunnerTest, RefusesBatchFiles) {
  // cmd.exe would parse their arguments again (CVE-2024-24576).
  for (const char* program : {"C:\\tools\\bw.cmd", "C:\\tools\\bw.BAT"}) {
    ProcessRequest request;
    request.program = program;
    Result<ProcessOutput> output = RunProcess(request);
    ASSERT_FALSE(output);
    EXPECT_NE(output.error().message.find("batch files"), std::string::npos);
  }
}
#endif

}  // namespace
}  // namespace brouk
