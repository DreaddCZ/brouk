#include "platform/win/link_resolver.h"

#include <objbase.h>

#include <algorithm>
#include <array>
#include <system_error>
#include <utility>

#include "cef/function_task.h"
#include "include/cef_request_context.h"
#include "include/cef_request_context_handler.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/data_paths.h"
#include "platform/win/theme.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukLinkCheck";
constexpr wchar_t kFolderPrefix[] = L"link-check-";
// How long a wrapper may stay on its own page before the user sees it.
constexpr UINT kShowAfterMs = 5000;
constexpr UINT_PTR kShowTimer = 1;
// The neutral context's data goes once Chromium has let go of it.
constexpr std::int64_t kRemoveAfterMs = 5000;

bool RegisterWindowClass() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

std::wstring NewFolderName() {
  GUID guid = {};
  std::array<wchar_t, 64> text = {};
  if (FAILED(CoCreateGuid(&guid)) ||
      StringFromGUID2(guid, text.data(), static_cast<int>(text.size())) == 0) {
    return std::wstring(kFolderPrefix) + std::to_wstring(GetTickCount64());
  }
  std::wstring name(text.data());
  std::erase_if(name, [](wchar_t c) { return c == L'{' || c == L'}'; });
  return kFolderPrefix + name;
}

}  // namespace

// One opaque link being followed, with its own context, browser and
// (hidden until needed) "Checking link" window.
class LinkResolver::Resolution final : public LinkResolverDelegate {
 public:
  Resolution(const std::string& url,
             const std::string& wrapper_name,
             std::vector<LinkWrapperRule> user_wrappers,
             HMONITOR monitor,
             std::function<void(int)> browser_count,
             Done done,
             std::function<void(Resolution&)> finished)
      : wrapper_name_(Utf8ToWide(wrapper_name)),
        monitor_(monitor),
        browser_count_(std::move(browser_count)),
        done_(std::move(done)),
        finished_(std::move(finished)) {
    CEF_REQUIRE_UI_THREAD();
    if (RegisterWindowClass()) {
      window_ = CreateWindowExW(0, kWindowClass, Title(L"").c_str(),
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, 820, 600, nullptr,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    if (!window_) {
      Finish(std::nullopt);
      return;
    }
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(window_, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(&Resolution::WindowProc));
    ApplyThemeToFrame(window_, CurrentTheme());
    // Its own folder, deleted afterwards: nothing it stores outlives this
    // resolution, and it shares nothing with any profile, as on macOS.
    folder_ = WebDataRootDirectory() / NewFolderName();
    CefRequestContextSettings context_settings;
    CefString(&context_settings.cache_path) = folder_.wstring();
    CefRefPtr<CefRequestContext> context =
        CefRequestContext::CreateContext(context_settings, nullptr);
    client_ = new LinkResolverClient(this, std::move(user_wrappers));
    RECT bounds = {};
    GetClientRect(window_, &bounds);
    CefWindowInfo info;
    info.SetAsChild(window_, CefRect(0, 0, bounds.right, bounds.bottom));
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    CefBrowserHost::CreateBrowser(info, client_, url, CefBrowserSettings(),
                                  nullptr, context);
    SetTimer(window_, kShowTimer, kShowAfterMs, nullptr);
  }
  ~Resolution() override {
    if (client_) {
      client_->DetachDelegate();
    }
    if (window_) {
      SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
      DestroyWindow(window_);
    }
  }
  Resolution(const Resolution&) = delete;
  Resolution& operator=(const Resolution&) = delete;

  void Cancel() {
    Finish(std::nullopt);
    if (closing_) {
      return;
    }
    closing_ = true;
    if (window_) {
      KillTimer(window_, kShowTimer);
      ShowWindow(window_, SW_HIDE);
    }
    if (browser_) {
      browser_->GetHost()->CloseBrowser(true);
    }
  }

  // LinkResolverDelegate (UI thread):
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser) override {
    browser_ = browser;
    browser_count_(1);
    if (closing_) {
      browser_->GetHost()->CloseBrowser(true);
    }
  }
  void OnResolved(const std::string& url) override {
    // Called from inside a navigation, where the browser must not close.
    std::weak_ptr<int> alive = alive_;
    CefPostTask(TID_UI, new FunctionTask([this, alive, url] {
                  if (!alive.expired()) {
                    Finish(url);
                    Cancel();
                  }
                }));
  }
  void OnCloseReady() override {
    if (browser_) {
      DestroyWindow(browser_->GetHost()->GetWindowHandle());
    }
  }
  void OnBrowserClosed() override {
    const bool counted = browser_ != nullptr;
    browser_ = nullptr;
    if (client_) {
      client_->DetachDelegate();
      client_ = nullptr;
    }
    // The context's data goes with it (once Chromium has let go of it).
    const std::filesystem::path folder = folder_;
    CefPostDelayedTask(TID_FILE_USER_BLOCKING, new FunctionTask([folder] {
                         std::error_code ignored;
                         std::filesystem::remove_all(folder, ignored);
                       }),
                       kRemoveAfterMs);
    if (counted) {
      browser_count_(-1);
    }
    // May delete this resolution later (posted).
    std::function<void(Resolution&)> finished = std::move(finished_);
    finished_ = nullptr;
    if (finished) {
      finished(*this);
    }
  }
  void OnTitleChanged(const std::string& title) override {
    if (window_) {
      SetWindowTextW(window_, Title(Utf8ToWide(title)).c_str());
    }
  }

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam) {
    auto* self =
        reinterpret_cast<Resolution*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!self) {
      return DefWindowProcW(window, message, wparam, lparam);
    }
    switch (message) {
      case WM_TIMER:
        if (wparam == kShowTimer) {
          KillTimer(window, kShowTimer);
          self->ShowToUser();
          return 0;
        }
        break;
      case WM_SIZE:
        if (self->browser_) {
          SetWindowPos(self->browser_->GetHost()->GetWindowHandle(), nullptr, 0,
                       0, LOWORD(lparam), HIWORD(lparam),
                       SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
      case WM_CLOSE:
        // Closing the window cancels the link.
        self->Cancel();
        return 0;
      default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }

  std::wstring Title(const std::wstring& page) const {
    const std::wstring base = wrapper_name_.empty()
                                  ? L"Checking link"
                                  : L"Checking link · via " + wrapper_name_;
    return page.empty() ? base : base + L" — " + page;
  }

  // The wrapper has a page of its own: the user decides.
  void ShowToUser() {
    if (closing_ || !window_) {
      return;
    }
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(monitor_
                        ? monitor_
                        : MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY),
                    &info);
    RECT frame = {};
    GetWindowRect(window_, &frame);
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    SetWindowPos(window_, HWND_TOP,
                 (info.rcWork.left + info.rcWork.right - width) / 2,
                 (info.rcWork.top + info.rcWork.bottom - height) / 2, width,
                 height, SWP_SHOWWINDOW);
    SetForegroundWindow(window_);
  }

  void Finish(std::optional<std::string> url) {
    Done done = std::move(done_);
    done_ = nullptr;
    if (done) {
      done(std::move(url));
    }
  }

  std::wstring wrapper_name_;
  HMONITOR monitor_;
  std::function<void(int)> browser_count_;
  Done done_;
  std::function<void(Resolution&)> finished_;
  HWND window_ = nullptr;
  std::filesystem::path folder_;
  CefRefPtr<LinkResolverClient> client_;
  CefRefPtr<CefBrowser> browser_;
  bool closing_ = false;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

LinkResolver::LinkResolver(std::function<void(int)> browser_count)
    : browser_count_(std::move(browser_count)) {}

LinkResolver::~LinkResolver() = default;

void LinkResolver::Resolve(const std::string& url,
                           const std::string& wrapper_name,
                           std::vector<LinkWrapperRule> user_wrappers,
                           HMONITOR monitor,
                           Done done) {
  resolutions_.push_back(std::make_unique<Resolution>(
      url, wrapper_name, std::move(user_wrappers), monitor, browser_count_,
      std::move(done), [this](Resolution& finished) {
        auto it = std::ranges::find_if(resolutions_, [&](const auto& item) {
          return item.get() == &finished;
        });
        if (it == resolutions_.end()) {
          return;
        }
        // Deleted from a task: this runs inside the resolution.
        std::shared_ptr<Resolution> gone(it->release());
        resolutions_.erase(it);
        CefPostTask(TID_UI, new FunctionTask([gone] {}));
      }));
}

void LinkResolver::CancelAll() {
  for (const std::unique_ptr<Resolution>& resolution : resolutions_) {
    resolution->Cancel();
  }
}

void LinkResolver::RemoveLeftoverData(
    const std::filesystem::path& web_data_root) {
  std::error_code error;
  for (auto it = std::filesystem::directory_iterator(web_data_root, error);
       !error && it != std::filesystem::directory_iterator();
       it.increment(error)) {
    if (it->path().filename().wstring().starts_with(kFolderPrefix)) {
      std::error_code ignored;
      std::filesystem::remove_all(it->path(), ignored);
    }
  }
}

}  // namespace brouk::win
