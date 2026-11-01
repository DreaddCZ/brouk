#include "platform/win/updater.h"

#include <windows.h>

#include <cstdio>
#include <system_error>
#include <utility>

#include "cef/function_task.h"
#include "core/crypto.h"
#include "core/text_file.h"
#include "include/base/cef_logging.h"
#include "include/cef_request_context.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr std::int64_t kFirstCheckDelayMs = 60 * 1000;
constexpr std::int64_t kCheckIntervalMs = 5 * 60 * 60 * 1000;  // 019.
// A feed is small; more is not a feed.
constexpr std::size_t kMaxFeedBytes = 1024 * 1024;

struct FileCloser {
  void operator()(std::FILE* file) const { std::fclose(file); }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

// Whether the installer at `path` is `size` bytes and signed with `key`.
// Reads the whole file (the signature covers it all, as Sparkle signs).
bool InstallerIsSigned(const std::filesystem::path& path,
                       const UpdateRelease& release,
                       const std::string& key) {
  Result<std::string> data =
      ReadTextFile(path, static_cast<std::uintmax_t>(release.size));
  return data &&
         data.value().size() == static_cast<std::size_t>(release.size) &&
         VerifyEd25519(key, release.signature, data.value());
}

}  // namespace

// One download: into memory (the feed) or into a file (the installer).
// Public files: no cookies or credentials are sent, nothing is cached. UI
// thread.
class Updater::Fetch : public CefURLRequestClient {
 public:
  // Body or error.
  using Done =
      std::function<void(std::optional<std::string> body, std::string error)>;

  static CefRefPtr<Fetch> Start(const std::string& url,
                                std::size_t max_bytes,
                                File file,
                                Done done) {
    CEF_REQUIRE_UI_THREAD();
    CefRefPtr<CefRequest> request = CefRequest::Create();
    request->SetURL(url);
    request->SetMethod("GET");
    request->SetFlags(UR_FLAG_DISABLE_CACHE);
    CefRefPtr<Fetch> fetch(
        new Fetch(max_bytes, std::move(file), std::move(done)));
    fetch->request_ = CefURLRequest::Create(
        request, fetch, CefRequestContext::GetGlobalContext());
    return fetch;
  }

  void Cancel() {
    done_ = nullptr;
    if (request_) {
      request_->Cancel();
    }
  }

  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    CEF_REQUIRE_UI_THREAD();
    if (!done_) {
      return;
    }
    Done done = std::move(done_);
    done_ = nullptr;
    const bool written = !file_ || std::fflush(file_.get()) == 0;
    file_.reset();
    CefRefPtr<CefResponse> response = request->GetResponse();
    const int status = response ? response->GetStatus() : 0;
    if (failed_) {
      done(std::nullopt, "The download is larger than announced.");
    } else if (!written) {
      done(std::nullopt, "The download could not be saved.");
    } else if (request->GetRequestStatus() != UR_SUCCESS) {
      done(std::nullopt, "No connection to the update server (network error " +
                             std::to_string(request->GetRequestError()) + ").");
    } else if (status != 200) {
      done(std::nullopt,
           "The update server answered " + std::to_string(status) + ".");
    } else {
      done(std::move(body_), "");
    }
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest> /*request*/,
                        int64_t /*current*/,
                        int64_t /*total*/) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> /*request*/,
                          int64_t /*current*/,
                          int64_t /*total*/) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request,
                      const void* data,
                      size_t data_length) override {
    CEF_REQUIRE_UI_THREAD();
    received_ += data_length;
    if (failed_ || received_ > max_bytes_ ||
        (file_ &&
         std::fwrite(data, 1, data_length, file_.get()) != data_length)) {
      failed_ = true;
      request->Cancel();
      return;
    }
    if (!file_) {
      body_.append(static_cast<const char*>(data), data_length);
    }
  }
  bool GetAuthCredentials(bool /*is_proxy*/,
                          const CefString& /*host*/,
                          int /*port*/,
                          const CefString& /*realm*/,
                          const CefString& /*scheme*/,
                          CefRefPtr<CefAuthCallback> /*callback*/) override {
    return false;
  }

 private:
  Fetch(std::size_t max_bytes, File file, Done done)
      : max_bytes_(max_bytes), file_(std::move(file)), done_(std::move(done)) {}

  const std::size_t max_bytes_;
  File file_;
  Done done_;
  std::size_t received_ = 0;
  bool failed_ = false;
  std::string body_;
  CefRefPtr<CefURLRequest> request_;

  IMPLEMENT_REFCOUNTING(Fetch);
};

Updater::Updater(Config config, std::function<void()> changed)
    : config_(std::move(config)), changed_(std::move(changed)) {
  std::error_code error;
  std::filesystem::remove_all(config_.folder, error);
}

Updater::~Updater() {
  if (fetch_) {
    fetch_->Cancel();
  }
}

void Updater::Stop() {
  CEF_REQUIRE_UI_THREAD();
  if (fetch_) {
    fetch_->Cancel();
    fetch_ = nullptr;
  }
  check_done_ = nullptr;
  // Pending checks and callbacks find it expired.
  alive_ = std::make_shared<int>(0);
  if (state_ == State::kChecking || state_ == State::kDownloading) {
    state_ = release_ ? State::kAvailable : State::kIdle;
  }
}

void Updater::SetSettings(const std::string& channel, const std::string& mode) {
  CEF_REQUIRE_UI_THREAD();
  const bool channel_changed = channel != channel_;
  const bool mode_changed = mode != mode_;
  channel_ = channel;
  mode_ = mode;
  if (!started_) {
    started_ = true;
    ScheduleCheck(kFirstCheckDelayMs);
    return;
  }
  if (channel_changed &&
      (state_ == State::kIdle || state_ == State::kAvailable)) {
    release_.reset();
    SetState(State::kIdle);
    ScheduleCheck(0);
  } else if (mode_changed && mode_ == "install" &&
             state_ == State::kAvailable) {
    Download();
  }
}

void Updater::CheckNow(CheckDone done) {
  CEF_REQUIRE_UI_THREAD();
  if (state_ == State::kDownloading || state_ == State::kReady) {
    done("");  // Already on its way: state() says so.
    return;
  }
  check_done_ = std::move(done);
  if (state_ != State::kChecking) {
    Check();
  }
}

void Updater::ScheduleCheck(std::int64_t delay_ms) {
  const int generation = ++schedule_generation_;
  std::weak_ptr<int> alive = alive_;
  CefPostDelayedTask(
      TID_UI, new FunctionTask([this, alive, generation] {
        if (alive.expired() || generation != schedule_generation_) {
          return;
        }
        ScheduleCheck(kCheckIntervalMs);
        // Off: only when asked (019, updates.mode).
        if (mode_ != "off" &&
            (state_ == State::kIdle || state_ == State::kAvailable)) {
          Check();
        }
      }),
      delay_ms);
}

void Updater::Check() {
  SetState(State::kChecking);
  std::weak_ptr<int> alive = alive_;
  fetch_ = Fetch::Start(
      config_.feed_url, kMaxFeedBytes, nullptr,
      [this, alive](std::optional<std::string> body, std::string error) {
        if (!alive.expired()) {
          OnFeed(std::move(body), error);
        }
      });
}

void Updater::OnFeed(std::optional<std::string> body,
                     const std::string& error) {
  fetch_ = nullptr;
  if (!body) {
    LOG(WARNING) << "Updates: " << error;
    SetState(release_ ? State::kAvailable : State::kIdle);
    FinishCheck(error);
    return;
  }
  Result<std::vector<UpdateRelease>> releases = ParseUpdateFeed(*body);
  if (!releases) {
    LOG(WARNING) << "Updates: " << releases.error().message;
    SetState(release_ ? State::kAvailable : State::kIdle);
    FinishCheck("The update feed could not be read.");
    return;
  }
  release_ = ChooseUpdate(releases.value(), config_.current_version, channel_);
  SetState(release_ ? State::kAvailable : State::kIdle);
  // Downloading first, so a check asked for reports it as such.
  if (release_ && mode_ == "install") {
    Download();
  }
  FinishCheck("");
}

void Updater::FinishCheck(const std::string& error) {
  if (CheckDone done = std::move(check_done_)) {
    check_done_ = nullptr;
    done(error);
  }
}

void Updater::Download() {
  CEF_REQUIRE_UI_THREAD();
  if (state_ != State::kAvailable || !release_) {
    return;
  }
  std::error_code error;
  std::filesystem::create_directories(config_.folder, error);
  const std::filesystem::path part = InstallerPath().wstring() + L".part";
  File file(_wfopen(part.c_str(), L"wb"));
  if (!file) {
    LOG(WARNING) << "Updates: cannot write to the updates folder.";
    return;
  }
  SetState(State::kDownloading);
  std::weak_ptr<int> alive = alive_;
  fetch_ = Fetch::Start(
      release_->url, static_cast<std::size_t>(release_->size), std::move(file),
      [this, alive](std::optional<std::string> body, std::string message) {
        if (!alive.expired()) {
          OnDownloaded(body.has_value(), message);
        }
      });
}

void Updater::OnDownloaded(bool ok, const std::string& error) {
  fetch_ = nullptr;
  const std::filesystem::path part = InstallerPath().wstring() + L".part";
  if (!ok || !release_) {
    LOG(WARNING) << "Updates: download failed: " << error;
    std::error_code ignored;
    std::filesystem::remove(part, ignored);
    SetState(State::kAvailable);
    return;
  }
  // Reading and checking a large file: off the UI thread.
  std::weak_ptr<int> alive = alive_;
  CefPostTask(
      TID_FILE_USER_VISIBLE,
      new FunctionTask([this, alive, part, release = *release_,
                        key = config_.public_key, path = InstallerPath()] {
        bool signed_ok = InstallerIsSigned(part, release, key);
        std::error_code error;
        if (signed_ok) {
          std::filesystem::rename(part, path, error);
          signed_ok = !error;
        }
        if (!signed_ok) {
          std::filesystem::remove(part, error);
        }
        CefPostTask(TID_UI, new FunctionTask([this, alive, signed_ok] {
                      if (!alive.expired()) {
                        OnVerified(signed_ok);
                      }
                    }));
      }));
}

void Updater::OnVerified(bool ok) {
  if (!ok) {
    // Not what the release was signed as: never run it. The next check
    // tries again.
    LOG(WARNING) << "Updates: the downloaded installer's signature is wrong.";
    SetState(State::kAvailable);
    return;
  }
  SetState(State::kReady);
}

std::string Updater::version() const {
  return release_ && state_ != State::kIdle && state_ != State::kChecking
             ? release_->version
             : "";
}

void Updater::SetState(State state) {
  if (state != state_) {
    state_ = state;
    if (changed_) {
      changed_();
    }
  }
}

std::filesystem::path Updater::InstallerPath() const {
  return config_.folder /
         Utf8ToWide("setup-" + (release_ ? release_->version : "") + ".exe");
}

void Updater::InstallAtExit(bool relaunch) const {
  if (state_ != State::kReady || !release_) {
    return;
  }
  // Checked again: the file waited in a folder of its own since.
  const std::filesystem::path installer = InstallerPath();
  if (!InstallerIsSigned(installer, *release_, config_.public_key)) {
    return;
  }
  std::wstring command = L"\"" + installer.wstring() +
                         L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART";
  if (relaunch) {
    command += L" /RELAUNCH=1";
  }
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  // Not RunProcess: the installer must outlive Brouk, which waits for
  // nothing here. Its path and arguments are Brouk's own, not the feed's.
  if (CreateProcessW(  // nosemgrep: cpp-process-spawn
          installer.c_str(), command.data(), nullptr, nullptr, FALSE,
          DETACHED_PROCESS, nullptr, nullptr, &startup, &process)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
}

}  // namespace brouk::win
