// RunProcess on Windows (process_runner_posix.cc is the macOS and Linux
// version). Same contract: an argument vector (no shell), only the three
// pipes inherited, environment changes, and a timeout that kills the program.
#include "core/process_runner.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/crypto.h"

namespace brouk {

namespace {

// More output than any `bw` command prints; a runaway program is cut off.
constexpr std::size_t kMaxOutput = std::size_t{64} * 1024 * 1024;
constexpr DWORD kChunk = 65536;

// Owns a Windows handle.
class Handle {
 public:
  Handle() = default;
  explicit Handle(HANDLE handle) : handle_(handle) {}
  ~Handle() { Close(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}
  Handle& operator=(Handle&& other) noexcept {
    if (this != &other) {
      Close();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  HANDLE get() const { return handle_; }
  bool valid() const {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }
  void Close() {
    if (valid()) {
      CloseHandle(handle_);
    }
    handle_ = nullptr;
  }

 private:
  HANDLE handle_ = nullptr;
};

struct Pipe {
  Handle read;
  Handle write;
};

// A pipe the program inherits one end of (the read end if `child_reads`);
// Brouk's end is not inheritable.
std::optional<Pipe> MakePipe(bool child_reads) {
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  HANDLE read = nullptr;
  HANDLE write = nullptr;
  if (!CreatePipe(&read, &write, &attributes, 0)) {
    return std::nullopt;
  }
  Pipe pipe{Handle(read), Handle(write)};
  HANDLE ours = child_reads ? pipe.write.get() : pipe.read.get();
  if (!SetHandleInformation(ours, HANDLE_FLAG_INHERIT, 0)) {
    return std::nullopt;
  }
  return pipe;
}

// Owns the attribute list that limits what the program inherits.
class AttributeList {
 public:
  AttributeList() {
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    storage_.resize(size);
    if (InitializeProcThreadAttributeList(get(), 1, 0, &size)) {
      initialized_ = true;
    }
  }
  ~AttributeList() {
    if (initialized_) {
      DeleteProcThreadAttributeList(get());
    }
  }
  AttributeList(const AttributeList&) = delete;
  AttributeList& operator=(const AttributeList&) = delete;

  bool initialized() const { return initialized_; }
  LPPROC_THREAD_ATTRIBUTE_LIST get() {
    return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
  }

 private:
  std::vector<std::byte> storage_;
  bool initialized_ = false;
};

std::wstring Widen(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  int length = static_cast<int>(text.size());
  int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), length, result.data(), size);
  return result;
}

std::string Narrow(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }
  int length = static_cast<int>(text.size());
  int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0,
                                 nullptr, nullptr);
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size,
                      nullptr, nullptr);
  return result;
}

void WipeWide(std::wstring& secret) {
  SecureZeroMemory(secret.data(), secret.size() * sizeof(wchar_t));
  secret.clear();
}

std::string ErrorText(DWORD error) {
  if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
    return "not found";
  }
  return "Windows error " + std::to_string(error);
}

// Appends one argument to a command line, quoted so that the program's C
// runtime (the rules of CommandLineToArgvW) reads it back unchanged:
// backslashes are literal except before a quote, where they are doubled.
void AppendArgument(std::wstring& line, std::wstring_view argument) {
  if (!line.empty()) {
    line += L' ';
  }
  if (!argument.empty() &&
      argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
    line += argument;
    return;
  }
  line += L'"';
  std::size_t backslashes = 0;
  for (wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    line.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
    backslashes = 0;
    line += c;
  }
  line.append(backslashes * 2, L'\\');
  line += L'"';
}

// Variable names are case-insensitive on Windows. Names may start with "="
// (the current folder of each drive, "=C:=C:\..."), so the name ends at the
// first "=" after the first character.
std::wstring_view VariableName(std::wstring_view entry) {
  return entry.substr(0, entry.find(L'=', 1));
}

bool SameName(std::wstring_view a, std::wstring_view b) {
  return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                              static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

// Brouk's environment with the request's changes, as an environment block
// ("NAME=value\0...\0\0"), sorted by name as Windows expects.
std::wstring ChildEnvironment(const ProcessRequest& request) {
  std::vector<std::wstring> replaced;
  for (const std::string& name : request.remove_environment) {
    replaced.push_back(Widen(name));
  }
  for (const auto& pair : request.environment) {
    replaced.push_back(Widen(pair.first));
  }
  std::vector<std::wstring> variables;
  if (wchar_t* block = GetEnvironmentStringsW()) {
    for (const wchar_t* entry = block; *entry != L'\0';
         entry += std::wcslen(entry) + 1) {
      std::wstring_view name = VariableName(entry);
      if (std::ranges::none_of(replaced, [&](const std::wstring& other) {
            return SameName(name, other);
          })) {
        variables.emplace_back(entry);
      }
    }
    FreeEnvironmentStringsW(block);
  }
  for (const auto& [name, value] : request.environment) {
    std::wstring variable = Widen(name);
    variable += L'=';
    std::wstring wide_value = Widen(value);
    variable += wide_value;
    WipeWide(wide_value);
    variables.push_back(std::move(variable));
  }
  std::ranges::sort(variables, [](const std::wstring& a,
                                  const std::wstring& b) {
    std::wstring_view name_a = VariableName(a);
    std::wstring_view name_b = VariableName(b);
    return CompareStringOrdinal(name_a.data(), static_cast<int>(name_a.size()),
                                name_b.data(), static_cast<int>(name_b.size()),
                                TRUE) == CSTR_LESS_THAN;
  });
  std::wstring block;
  for (std::wstring& variable : variables) {
    block += variable;
    block += L'\0';
    WipeWide(variable);
  }
  block += L'\0';
  if (block.size() == 1) {
    block += L'\0';  // An empty block still needs two terminators.
  }
  return block;
}

// cmd.exe parses the command line of .bat and .cmd files again with its own
// rules, so their arguments cannot be passed safely (as in CVE-2024-24576).
bool IsBatchFile(const std::filesystem::path& program) {
  std::wstring extension = program.extension().wstring();
  return SameName(extension, L".bat") || SameName(extension, L".cmd");
}

}  // namespace

Result<ProcessOutput> RunProcess(const ProcessRequest& request) {
  std::string program_name = Narrow(request.program.native());
  if (IsBatchFile(request.program)) {
    return Error{"Could not run " + program_name +
                 ": batch files are not supported, use the .exe"};
  }
  std::optional<Pipe> in = MakePipe(/*child_reads=*/true);
  std::optional<Pipe> out = MakePipe(/*child_reads=*/false);
  std::optional<Pipe> err = MakePipe(/*child_reads=*/false);
  if (!in || !out || !err) {
    return Error{"Could not create pipes: " + ErrorText(GetLastError())};
  }

  // The program and everything it starts run in a job, so a timeout ends
  // them all, and nothing outlives Brouk's wait for it.
  Handle job(CreateJobObjectW(nullptr, nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job.valid() ||
      !SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                               &limits, sizeof(limits))) {
    return Error{"Could not create a job: " + ErrorText(GetLastError())};
  }

  // The program inherits only the three pipes, none of Brouk's other
  // handles.
  std::array<HANDLE, 3> inherited = {in->read.get(), out->write.get(),
                                     err->write.get()};
  AttributeList attributes;
  if (!attributes.initialized() ||
      !UpdateProcThreadAttribute(
          attributes.get(), 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
          inherited.data(), sizeof(inherited), nullptr, nullptr)) {
    return Error{"Could not limit inherited handles: " +
                 ErrorText(GetLastError())};
  }
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = in->read.get();
  startup.StartupInfo.hStdOutput = out->write.get();
  startup.StartupInfo.hStdError = err->write.get();
  startup.lpAttributeList = attributes.get();

  // CreateProcessW wants Windows separators.
  std::wstring program =
      std::filesystem::path(request.program).make_preferred().wstring();
  std::wstring command_line;
  AppendArgument(command_line, program);
  for (const std::string& argument : request.arguments) {
    AppendArgument(command_line, Widen(argument));
  }
  std::wstring environment = ChildEnvironment(request);

  // Suspended until it is in the job, so nothing it starts escapes the job.
  PROCESS_INFORMATION info{};
  BOOL created = CreateProcessW(
      program.c_str(), command_line.data(), nullptr, nullptr, TRUE,
      CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT |
          CREATE_NO_WINDOW | CREATE_SUSPENDED,
      environment.data(), nullptr, &startup.StartupInfo, &info);
  DWORD create_error = GetLastError();
  WipeWide(environment);
  if (!created) {
    return Error{"Could not run " + program_name + ": " +
                 ErrorText(create_error)};
  }
  Handle process(info.hProcess);
  Handle thread(info.hThread);
  if (!AssignProcessToJobObject(job.get(), process.get())) {
    DWORD error = GetLastError();
    TerminateProcess(process.get(), 1);
    return Error{"Could not run " + program_name + ": " + ErrorText(error)};
  }
  ResumeThread(thread.get());
  in->read.Close();
  out->write.Close();
  err->write.Close();
  if (request.input.empty()) {
    in->write.Close();
  }

  // Anonymous pipes have no asynchronous I/O on Windows: one thread per
  // pipe. Reading ends when the program, and everything it started, has
  // closed its end; ending the job closes them all.
  ProcessOutput output;
  std::mutex mutex;
  std::condition_variable reads_done;
  int finished_reads = 0;  // Guarded by `mutex`.
  auto read_all = [&](Handle& source, std::string& target) {
    // Holds output, which may be secret: wiped at the end.
    std::string buffer(kChunk, '\0');
    DWORD n = 0;
    while (ReadFile(source.get(), buffer.data(), kChunk, &n, nullptr) &&
           n > 0) {
      if (target.size() < kMaxOutput) {
        target.append(buffer.data(), n);
      }
    }
    Wipe(buffer);
    {
      std::lock_guard<std::mutex> lock(mutex);
      ++finished_reads;
    }
    reads_done.notify_one();
  };
  std::thread out_reader(read_all, std::ref(out->read), std::ref(output.out));
  std::thread err_reader(read_all, std::ref(err->read), std::ref(output.err));
  std::thread writer;
  if (in->write.valid()) {
    writer = std::thread([&] {
      std::size_t written = 0;
      while (written < request.input.size()) {
        DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(request.input.size() - written, kChunk));
        DWORD n = 0;
        // Fails once the program has stopped reading (a broken pipe).
        if (!WriteFile(in->write.get(), request.input.data() + written, chunk,
                       &n, nullptr)) {
          break;
        }
        written += n;
      }
      in->write.Close();
    });
  }

  auto deadline = std::chrono::steady_clock::now() + request.timeout;
  {
    std::unique_lock<std::mutex> lock(mutex);
    output.timed_out = !reads_done.wait_until(
        lock, deadline, [&] { return finished_reads == 2; });
  }
  if (!output.timed_out) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    DWORD wait = static_cast<DWORD>(std::max<std::int64_t>(left.count(), 0));
    output.timed_out = WaitForSingleObject(process.get(), wait) == WAIT_TIMEOUT;
  }
  // Ends the program if it timed out, and anything it left running; their
  // pipe ends close, so the threads finish.
  TerminateJobObject(job.get(), 1);
  out_reader.join();
  err_reader.join();
  if (writer.joinable()) {
    writer.join();
  }
  WaitForSingleObject(process.get(), INFINITE);

  DWORD exit_code = 0;
  GetExitCodeProcess(process.get(), &exit_code);
  // A crash ends a program with an NTSTATUS error code (0xC...).
  bool crashed = (exit_code & 0xC0000000U) == 0xC0000000U;
  output.exit_code =
      output.timed_out || crashed ? -1 : static_cast<int>(exit_code);
  return output;
}

}  // namespace brouk
