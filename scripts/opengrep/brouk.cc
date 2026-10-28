// Test cases for brouk.yml (see brouk.ts). Test mode ignores the rules'
// "paths", so the rules limited to src/ are checked here too.

void Cases(char* buffer, const char* text, int* p, Widget* w) {
  // ruleid: cpp-process-spawn
  std::system("ls");
  // ruleid: cpp-process-spawn
  posix_spawnp(&pid, "bw", nullptr, nullptr, argv, env);
  // ruleid: cpp-process-spawn
  execvp("bw", argv);
  // ruleid: cpp-process-spawn
  CreateProcessW(program, line, nullptr, nullptr, TRUE, 0, env, nullptr, &si, &pi);
  // ruleid: cpp-process-spawn
  ShellExecuteW(nullptr, L"open", file, nullptr, nullptr, SW_SHOW);
  // ruleid: cpp-process-spawn
  _wspawnvp(_P_WAIT, L"bw", argv);
  // ok: cpp-process-spawn
  file_system(path);
  // ok: cpp-process-spawn
  CreateProcessThing(x);

  // ruleid: cpp-unsafe-c-function
  strcpy(buffer, text);
  // ruleid: cpp-unsafe-c-function
  std::sprintf(buffer, "%d", 1);
  // ruleid: cpp-unsafe-c-function
  int n = atoi(text);
  // ok: cpp-unsafe-c-function
  std::snprintf(buffer, 4, "%d", 1);

  // ruleid: cpp-delete
  delete p;
  // ruleid: cpp-delete
  delete[] p;
  // ok: cpp-delete
  const char* js = "delete window.x;";

  // ruleid: log-secret
  LOG(ERROR) << "unlock failed for " << master_password;
  // ruleid: log-secret
  LOG(INFO) << "cookie: " << request.cookie_header();
  // ok: log-secret
  LOG(ERROR) << "Passwords of profile " << profile << ": " << last_error;
}

class Widget {
  // ok: cpp-delete
  Widget(const Widget&) = delete;
};
