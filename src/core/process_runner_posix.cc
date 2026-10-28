// RunProcess on macOS and Linux (process_runner_win.cc is the Windows
// version).
#include "core/process_runner.h"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <optional>

#include "core/crypto.h"

extern char** environ;  // NOLINT: POSIX.

namespace brouk {

namespace {

// More output than any `bw` command prints; a runaway program is cut off.
constexpr std::size_t kMaxOutput = std::size_t{64} * 1024 * 1024;

// Owns a file descriptor.
class Fd {
 public:
  Fd() = default;
  explicit Fd(int fd) : fd_(fd) {}
  ~Fd() { Close(); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) {
      Close();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }
  void Close() {
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

 private:
  int fd_ = -1;
};

struct Pipe {
  Fd read;
  Fd write;
};

std::optional<Pipe> MakePipe() {
  std::array<int, 2> fds{};
  if (pipe(fds.data()) != 0) {
    return std::nullopt;
  }
  Pipe result{Fd(fds[0]), Fd(fds[1])};
  for (int fd : fds) {
    fcntl(fd, F_SETFD, FD_CLOEXEC);
  }
  return result;
}

// Owns posix_spawn's file actions and attributes.
class SpawnSetup {
 public:
  SpawnSetup() {
    posix_spawn_file_actions_init(&actions_);
    posix_spawnattr_init(&attributes_);
  }
  ~SpawnSetup() {
    posix_spawn_file_actions_destroy(&actions_);
    posix_spawnattr_destroy(&attributes_);
  }
  SpawnSetup(const SpawnSetup&) = delete;
  SpawnSetup& operator=(const SpawnSetup&) = delete;

  posix_spawn_file_actions_t* actions() { return &actions_; }
  posix_spawnattr_t* attributes() { return &attributes_; }

 private:
  posix_spawn_file_actions_t actions_{};
  posix_spawnattr_t attributes_{};
};

std::string VariableName(std::string_view entry) {
  return std::string(entry.substr(0, entry.find('=')));
}

// Brouk's environment with the request's changes, as "NAME=value" strings.
std::vector<std::string> ChildEnvironment(const ProcessRequest& request) {
  std::vector<std::string> variables;
  auto replaced = [&](const std::string& name) {
    return std::ranges::find(request.remove_environment, name) !=
               request.remove_environment.end() ||
           std::ranges::any_of(request.environment, [&](const auto& pair) {
             return pair.first == name;
           });
  };
  for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
    if (!replaced(VariableName(*entry))) {
      variables.emplace_back(*entry);
    }
  }
  for (const auto& [name, value] : request.environment) {
    std::string& variable = variables.emplace_back(name);
    variable += '=';
    variable += value;
  }
  return variables;
}

// Writing to a program that has stopped reading must not kill Brouk with
// SIGPIPE.
void NoSigpipe([[maybe_unused]] int fd) {
#ifdef F_SETNOSIGPIPE
  fcntl(fd, F_SETNOSIGPIPE, 1);
#endif
}

class BlockedSigpipe {
 public:
  BlockedSigpipe() {
#ifndef F_SETNOSIGPIPE
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &set, &previous_);
#endif
  }
  ~BlockedSigpipe() {
#ifndef F_SETNOSIGPIPE
    // Discards a SIGPIPE raised meanwhile, then restores the mask.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    timespec zero{};
    while (sigtimedwait(&set, nullptr, &zero) > 0) {
    }
    pthread_sigmask(SIG_SETMASK, &previous_, nullptr);
#endif
  }
  BlockedSigpipe(const BlockedSigpipe&) = delete;
  BlockedSigpipe& operator=(const BlockedSigpipe&) = delete;

 private:
#ifndef F_SETNOSIGPIPE
  sigset_t previous_{};
#endif
};

int ExitCodeOf(int status) {
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

}  // namespace

Result<ProcessOutput> RunProcess(const ProcessRequest& request) {
  std::optional<Pipe> in = MakePipe();
  std::optional<Pipe> out = MakePipe();
  std::optional<Pipe> err = MakePipe();
  if (!in || !out || !err) {
    return Error{std::string("Could not create pipes: ") +
                 std::strerror(errno)};
  }
  SpawnSetup setup;
  posix_spawn_file_actions_adddup2(setup.actions(), in->read.get(), 0);
  posix_spawn_file_actions_adddup2(setup.actions(), out->write.get(), 1);
  posix_spawn_file_actions_adddup2(setup.actions(), err->write.get(), 2);
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
  // macOS: the program inherits only the three pipes, none of Brouk's other
  // files and sockets.
  posix_spawnattr_setflags(setup.attributes(), POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif

  std::string program = request.program.string();
  std::vector<std::string> arguments = {program};
  arguments.insert(arguments.end(), request.arguments.begin(),
                   request.arguments.end());
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 1);
  for (std::string& argument : arguments) {
    argv.push_back(argument.data());
  }
  argv.push_back(nullptr);
  std::vector<std::string> variables = ChildEnvironment(request);
  std::vector<char*> envp;
  envp.reserve(variables.size() + 1);
  for (std::string& variable : variables) {
    envp.push_back(variable.data());
  }
  envp.push_back(nullptr);

  pid_t pid = 0;
  int spawned = posix_spawn(&pid, program.c_str(), setup.actions(),
                            setup.attributes(), argv.data(), envp.data());
  for (std::string& variable : variables) {
    Wipe(variable);
  }
  if (spawned != 0) {
    return Error{"Could not run " + program + ": " + std::strerror(spawned)};
  }
  in->read.Close();
  out->write.Close();
  err->write.Close();
  NoSigpipe(in->write.get());
  fcntl(in->write.get(), F_SETFL, O_NONBLOCK);
  if (request.input.empty()) {
    in->write.Close();
  }

  BlockedSigpipe blocked;
  ProcessOutput output;
  std::size_t written = 0;
  auto deadline = std::chrono::steady_clock::now() + request.timeout;
  // Holds output, which may be secret: wiped at the end.
  std::string buffer(65536, '\0');
  while (out->read.valid() || err->read.valid()) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (left.count() <= 0) {
      output.timed_out = true;
      break;
    }
    std::array<pollfd, 3> fds{};
    nfds_t count = 0;
    auto watch = [&](const Fd& fd, short events) {
      if (fd.valid()) {
        fds[count++] = {fd.get(), events, 0};
      }
    };
    watch(out->read, POLLIN);
    watch(err->read, POLLIN);
    watch(in->write, POLLOUT);
    int ready = poll(fds.data(), count, static_cast<int>(left.count()));
    if (ready < 0 && errno != EINTR) {
      break;
    }
    for (nfds_t i = 0; i < count; ++i) {
      if (fds[i].revents == 0) {
        continue;
      }
      if (fds[i].fd == in->write.get()) {
        ssize_t n = write(in->write.get(), request.input.data() + written,
                          request.input.size() - written);
        if (n > 0) {
          written += static_cast<std::size_t>(n);
        }
        if ((n < 0 && errno != EAGAIN) || written == request.input.size()) {
          in->write.Close();
        }
        continue;
      }
      bool is_out = fds[i].fd == out->read.get();
      Fd& source = is_out ? out->read : err->read;
      std::string& target = is_out ? output.out : output.err;
      ssize_t n = read(source.get(), buffer.data(), buffer.size());
      if (n > 0 && target.size() < kMaxOutput) {
        target.append(buffer.data(), static_cast<std::size_t>(n));
      } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
        source.Close();
      }
    }
  }
  Wipe(buffer);
  in->write.Close();
  int status = 0;
  if (output.timed_out) {
    kill(pid, SIGKILL);
  }
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  output.exit_code = output.timed_out ? -1 : ExitCodeOf(status);
  return output;
}

}  // namespace brouk
