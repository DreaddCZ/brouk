#ifndef BROUK_CORE_PROCESS_RUNNER_H_
#define BROUK_CORE_PROCESS_RUNNER_H_

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "core/result.h"

namespace brouk {

// A program to run and wait for, such as the Bitwarden CLI (008).
//
// Arguments are visible to every process on the computer (`ps`), so secrets
// never go there: pass them in `environment` (readable only by the same
// user) or `input` (standard input).
struct ProcessRequest {
  std::filesystem::path program;
  std::vector<std::string> arguments;
  // Variables set for the program, on top of Brouk's own environment.
  std::vector<std::pair<std::string, std::string>> environment;
  // Variables of Brouk's environment the program must not inherit.
  std::vector<std::string> remove_environment;
  std::string input;
  std::chrono::milliseconds timeout{60000};
};

struct ProcessOutput {
  int exit_code = -1;  // -1 if it was killed or crashed.
  bool timed_out = false;
  std::string out;
  std::string err;
};

// Runs the program and waits for it to end (or kills it after the timeout).
// Blocks, so never call it on the UI thread. Fails only if the program could
// not be started. The output may hold secrets: the caller wipes it.
Result<ProcessOutput> RunProcess(const ProcessRequest& request);

}  // namespace brouk

#endif  // BROUK_CORE_PROCESS_RUNNER_H_
