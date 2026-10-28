// A program for process_runner_test that behaves the same on every system
// (the tests cannot rely on /bin/sh or cmd.exe). It runs its arguments as
// commands, in order:
//   cat           copies standard input to standard output
//   err <text>    writes <text> and a newline to standard error
//   args          writes each later argument on its own line, then stops
//   env <name>    writes the variable's value (empty if unset) and "|"
//   sleep <ms>    waits
//   exit <code>   ends with that exit code
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

int Number(const char* text) {
  int value = 0;
  std::from_chars(text, text + std::strlen(text), value);
  return value;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
  // Bytes as they are: no newline translation.
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  for (int i = 1; i < argc; ++i) {
    std::string command = argv[i];
    bool has_value = i + 1 < argc;
    if (command == "cat") {
      char buffer[65536];
      std::size_t n = 0;
      while ((n = std::fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
        std::fwrite(buffer, 1, n, stdout);
      }
    } else if (command == "err" && has_value) {
      std::fprintf(stderr, "%s\n", argv[++i]);
    } else if (command == "args") {
      for (++i; i < argc; ++i) {
        std::printf("%s\n", argv[i]);
      }
    } else if (command == "env" && has_value) {
      const char* value = std::getenv(argv[++i]);
      std::printf("%s|", value != nullptr ? value : "");
    } else if (command == "sleep" && has_value) {
      std::this_thread::sleep_for(std::chrono::milliseconds(Number(argv[++i])));
    } else if (command == "exit" && has_value) {
      std::fflush(stdout);
      return Number(argv[++i]);
    } else {
      std::fprintf(stderr, "unknown command: %s\n", command.c_str());
      return 100;
    }
    std::fflush(stdout);
    std::fflush(stderr);
  }
  return 0;
}
