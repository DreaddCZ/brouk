#include "platform/win/link_routing.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include <utility>

#include "core/link_unwrap.h"
#include "core/log.h"
#include "include/base/cef_logging.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/browser_window.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

// The screen the user is looking at: for a link from another app the one
// with the mouse pointer (they just clicked it).
HMONITOR MonitorWithMouse() {
  POINT point = {};
  GetCursorPos(&point);
  return MonitorFromPoint(point, MONITOR_DEFAULTTOPRIMARY);
}

Microsoft::WRL::ComPtr<IVirtualDesktopManager> DesktopManager() {
  Microsoft::WRL::ComPtr<IVirtualDesktopManager> manager;
  CoCreateInstance(CLSID_VirtualDesktopManager, nullptr, CLSCTX_ALL,
                   IID_IVirtualDesktopManager,
                   reinterpret_cast<void**>(manager.ReleaseAndGetAddressOf()));
  return manager;
}

}  // namespace

LinkRoutingController::LinkRoutingController(LinkRoutingHost& host,
                                             std::filesystem::path config_file)
    : host_(host),
      path_(std::move(config_file)),
      resolver_([this](int delta) { host_.RoutingBrowserCount(delta); }) {
  Reload();
}

LinkRoutingController::~LinkRoutingController() = default;

void LinkRoutingController::Reload() {
  problems_.clear();
  config_ = LoadLinkRouting(path_, problems_);
  for (const std::string& problem : problems_) {
    LogWarning() << "Link routing: " << problem;
  }
}

std::optional<Error> LinkRoutingController::SetConfig(
    LinkRoutingConfig config) {
  if (std::optional<Error> error = SaveLinkRouting(path_, config)) {
    return error;
  }
  config_ = std::move(config);
  problems_.clear();
  return std::nullopt;
}

// Entry points.

void LinkRoutingController::OpenExternalUrl(const std::string& url) {
  Request request;
  request.url = url;
  request.source = LinkSource::kOtherApp;
  // Windows does not say which app started Brouk with the link.
  request.source_name = L"From another app";
  request.foreground = true;
  request.monitor = MonitorWithMouse();
  Enqueue(std::move(request));
}

bool LinkRoutingController::RouteUrl(const std::string& url,
                                     LinkSource source,
                                     BrowserWindow& window,
                                     TabId tab,
                                     bool foreground) {
  Request request;
  request.url = url;
  request.source = source;
  request.source_profile = window.ProfileOfTab(tab);
  for (const ProfileInfo& info : host_.RoutingProfiles()) {
    if (info.id == request.source_profile) {
      request.source_name = L"From " + Utf8ToWide(info.name);
    }
  }
  request.window = &window;
  request.tab = tab;
  request.foreground = foreground;
  request.monitor = MonitorFromWindow(window.hwnd(), MONITOR_DEFAULTTONEAREST);
  // Decided here, so the caller knows whether to open the link itself.
  const UnwrappedLink link = UnwrapLink(url, config_.wrappers);
  if (link.kind != UnwrappedLink::Kind::kOpaque &&
      DecideProfileFor(request, link.url).stays_in_source) {
    return false;
  }
  Enqueue(std::move(request));
  return true;
}

void LinkRoutingController::OpenUrlInProfile(const std::string& url,
                                             const ProfileId& profile,
                                             const std::wstring& source_name,
                                             BrowserWindow* window,
                                             bool foreground,
                                             bool bookmark) {
  Request request;
  request.url = url;
  request.real_url = url;
  request.open_url = url;
  request.source = LinkSource::kNewTab;
  request.source_name = source_name;
  request.known_profile = profile;
  request.window = window;
  request.foreground = foreground;
  request.bookmark = bookmark;
  request.monitor =
      window ? MonitorFromWindow(window->hwnd(), MONITOR_DEFAULTTONEAREST)
             : MonitorWithMouse();
  EnqueueResolved(std::move(request));
}

void LinkRoutingController::CancelPending() {
  queue_.clear();
  resolver_.CancelAll();
  picker_.Cancel();
}

// Routing.

void LinkRoutingController::Enqueue(Request request) {
  if (picker_.visible()) {
    queue_.push_back(std::move(request));
    return;
  }
  Process(std::move(request));
}

void LinkRoutingController::EnqueueResolved(Request request) {
  if (picker_.visible()) {
    queue_.push_back(std::move(request));
    return;
  }
  Route(std::move(request));
}

// The picker closed: the next waiting link.
void LinkRoutingController::ProcessNext() {
  while (!queue_.empty() && !picker_.visible()) {
    Request next = std::move(queue_.front());
    queue_.pop_front();
    if (next.real_url) {
      Route(std::move(next));
    } else {
      Process(std::move(next));
    }
  }
}

void LinkRoutingController::Process(Request request) {
  const UnwrappedLink link = UnwrapLink(request.url, config_.wrappers);
  request.wrapper = link.wrapper;
  if (link.kind == UnwrappedLink::Kind::kOpaque) {
    // Followed in a neutral context; the final URL is routed like a link
    // from the original source (and loaded directly: the check has run).
    std::weak_ptr<int> alive = alive_;
    const HMONITOR monitor = request.monitor;
    const std::string url = request.url;
    const std::string wrapper = request.wrapper;
    resolver_.Resolve(
        url, wrapper, config_.wrappers, monitor,
        [this, alive, request](std::optional<std::string> final_url) mutable {
          if (alive.expired() || !final_url) {
            return;
          }
          request.real_url = *final_url;
          request.open_url = *final_url;
          EnqueueResolved(std::move(request));
        });
    return;
  }
  request.real_url = link.url;
  request.open_url = request.url;
  Route(std::move(request));
}

ProfileDecision LinkRoutingController::DecideProfileFor(
    const Request& request,
    const std::string& real_url) {
  std::vector<ProfileId> existing;
  for (const ProfileInfo& info : host_.RoutingProfiles()) {
    existing.push_back(info.id);
  }
  return DecideProfile(config_, real_url, request.source,
                       request.source_profile, host_.RoutingDefaultProfile(),
                       existing);
}

void LinkRoutingController::Route(Request request) {
  ProfileDecision decision;
  if (!request.known_profile.empty()) {
    decision.profiles.push_back(request.known_profile);
  } else {
    decision = DecideProfileFor(request, request.real_url.value_or(""));
    LogRouting(request, decision);
  }
  if (decision.stays_in_source) {
    OpenInSource(request);
    return;
  }
  if (decision.profiles.size() == 1) {
    const WindowDecision windows =
        DecideWindowFor(decision.profiles.front(), request);
    if (windows.kind != WindowDecision::Kind::kChoose) {
      Open(request, decision.profiles.front(), windows);
      return;
    }
  }
  ShowPicker(std::move(request), decision);
}

// One line in Brouk's log per routed link while debug logging is on
// (025), so a link that opens in an unexpected profile can be explained.
// Never query values: LinkForLog leaves them out.
void LinkRoutingController::LogRouting(const Request& request,
                                       const ProfileDecision& decision) {
  std::string profiles;
  for (const ProfileId& profile : decision.profiles) {
    profiles += (profiles.empty() ? "" : ", ") + profile;
  }
  const std::string rule = decision.rule_pattern
                               ? "rule " + *decision.rule_pattern
                               : std::string("no rule");
  LogInfo() << "Link routing: " << WideToUtf8(request.source_name) << ": "
            << LinkForLog(request.url) << ", via "
            << (request.wrapper.empty() ? "no wrapper" : request.wrapper)
            << ", routed by " << LinkForLog(request.real_url.value_or(""))
            << ": " << rule << " -> " << profiles;
}

bool LinkRoutingController::OnCurrentDesktop(HWND window) {
  // Made when needed: a COM object must not outlive COM (a static one would
  // be released after it has shut down, at exit).
  const Microsoft::WRL::ComPtr<IVirtualDesktopManager> manager =
      DesktopManager();
  BOOL current = TRUE;
  if (manager &&
      FAILED(manager->IsWindowOnCurrentVirtualDesktop(window, &current))) {
    current = TRUE;
  }
  return current != FALSE;
}

WindowDecision LinkRoutingController::DecideWindowFor(const ProfileId& profile,
                                                      const Request& request) {
  std::vector<RoutingWindow> windows;
  for (BrowserWindow* window : host_.RoutingWindows()) {
    RoutingWindow entry;
    entry.id = window->window_id();
    entry.profiles = window->GroupProfiles();
    entry.on_current_screen =
        request.monitor &&
        MonitorFromWindow(window->hwnd(), MONITOR_DEFAULTTONEAREST) ==
            request.monitor;
    entry.on_active_space = OnCurrentDesktop(window->hwnd());
    windows.push_back(std::move(entry));
  }
  return DecideWindow(profile, host_.RoutingDefaultProfile(), windows);
}

BrowserWindow* LinkRoutingController::WindowWithId(std::int64_t id) {
  for (BrowserWindow* window : host_.RoutingWindows()) {
    if (window->window_id() == id) {
      return window;
    }
  }
  return nullptr;
}

// The picker.

void LinkRoutingController::ShowPicker(Request request,
                                       const ProfileDecision& decision) {
  std::vector<LinkPickerProfile> profiles;
  for (const ProfileId& id : decision.profiles) {
    LinkPickerProfile profile;
    profile.id = id;
    for (const ProfileInfo& info : host_.RoutingProfiles()) {
      if (info.id == id) {
        profile.name = Utf8ToWide(info.name);
      }
    }
    profile.color = host_.RoutingProfileColor(id);
    profiles.push_back(std::move(profile));
  }
  std::optional<std::wstring> pattern;
  std::optional<std::string> rule;
  if (decision.rule_pattern) {
    pattern = Utf8ToWide(*decision.rule_pattern);
    rule = *decision.rule_pattern;
  }
  std::weak_ptr<int> alive = alive_;
  const Request shown = request;
  picker_.Show(
      Utf8ToWide(request.real_url.value_or(request.url)),
      Utf8ToWide(request.wrapper), request.source_name, std::move(profiles),
      pattern,
      [this, alive, shown](const ProfileId& profile) {
        return alive.expired() ? std::vector<LinkPickerWindow>{}
                               : PickerWindowsFor(profile, shown);
      },
      request.monitor,
      [this, alive, shown, rule](const LinkPickerResult& result) {
        if (!alive.expired()) {
          PickerDone(result, shown, rule);
        }
      });
}

// The windows to offer for `profile`; empty when routing decides alone.
std::vector<LinkPickerWindow> LinkRoutingController::PickerWindowsFor(
    const ProfileId& profile,
    const Request& request) {
  std::vector<LinkPickerWindow> windows;
  const WindowDecision decision = DecideWindowFor(profile, request);
  if (decision.kind != WindowDecision::Kind::kChoose) {
    return windows;
  }
  for (std::int64_t id : decision.choices) {
    BrowserWindow* window = WindowWithId(id);
    if (!window) {
      continue;
    }
    LinkPickerWindow entry;
    entry.id = id;
    entry.window = window->hwnd();
    entry.title = window->ActiveTabTitle();
    for (const TabStripModel::Group& group : window->TabGroups()) {
      if (group.count == 0) {
        continue;
      }
      LinkPickerGroup chip;
      for (const ProfileInfo& info : host_.RoutingProfiles()) {
        if (info.id == group.profile) {
          chip.name = Utf8ToWide(info.name);
        }
      }
      chip.color = host_.RoutingProfileColor(group.profile);
      chip.tabs = group.count;
      entry.groups.push_back(std::move(chip));
    }
    windows.push_back(std::move(entry));
  }
  return windows;
}

void LinkRoutingController::PickerDone(
    const LinkPickerResult& result,
    const Request& request,
    const std::optional<std::string>& pattern) {
  if (!result.cancelled) {
    if (result.always && pattern) {
      LinkRoutingConfig config = config_;
      if (NarrowRoutingRule(config, *pattern, result.profile)) {
        if (std::optional<Error> error = SetConfig(config)) {
          LogWarning() << "Link routing: " << error->message;
        }
      }
    }
    WindowDecision windows;
    switch (result.window) {
      case LinkPickerResult::Window::kNotAsked:
        windows = DecideWindowFor(result.profile, request);
        if (windows.kind == WindowDecision::Kind::kChoose) {
          // Windows changed while the picker was open: the most recent.
          windows.kind = WindowDecision::Kind::kWindow;
          windows.window = windows.choices.front();
        }
        break;
      case LinkPickerResult::Window::kExisting:
        windows.kind = WindowDecision::Kind::kWindow;
        windows.window = result.window_id;
        break;
      case LinkPickerResult::Window::kNewWindow:
        windows.kind = WindowDecision::Kind::kNewWindow;
        break;
    }
    Open(request, result.profile, windows);
  }
  ProcessNext();
}

// Opening.

// The link stays in its source tab's profile, as without routing.
void LinkRoutingController::OpenInSource(const Request& request) {
  BrowserWindow* window =
      host_.IsRoutingWindow(request.window) ? request.window : nullptr;
  if (!window) {
    // The source window is gone: its profile's group decides.
    Open(request, request.source_profile,
         DecideWindowFor(request.source_profile, request));
    return;
  }
  if (request.source == LinkSource::kTypedNewTab) {
    window->LoadTypedUrlInTab(request.tab, request.open_url);
    return;
  }
  window->OpenUrlNextToTab(request.tab, request.open_url,
                           request.source_profile, request.foreground);
}

void LinkRoutingController::Open(const Request& request,
                                 const ProfileId& profile,
                                 const WindowDecision& windows) {
  BrowserWindow* target = nullptr;
  switch (windows.kind) {
    case WindowDecision::Kind::kWindow:
    case WindowDecision::Kind::kNewGroup:
      target = WindowWithId(windows.window);
      break;
    case WindowDecision::Kind::kChoose:
      target = windows.choices.empty() ? nullptr
                                       : WindowWithId(windows.choices.front());
      break;
    case WindowDecision::Kind::kNewWindow:
      break;
  }
  const bool new_window = target == nullptr;
  if (new_window) {
    target = host_.OpenRoutingWindow();
    if (!target) {
      return;
    }
  }
  BrowserWindow* source =
      host_.IsRoutingWindow(request.window) ? request.window : nullptr;
  // A background tab (Ctrl-click) stays in the background in its own
  // window; anywhere else it is shown, so it can be found.
  const bool activate = request.foreground || target != source;
  target->OpenTab(request.open_url, activate, profile, request.bookmark);
  if (new_window) {
    target->Show();
  }
  if (request.foreground || new_window) {
    BringToFront(*target);
  }
  if (request.source == LinkSource::kTypedNewTab && source) {
    source->CloseTabIfUnused(request.tab);
  }
}

// Brings `window` to the front. On another virtual desktop Windows
// switches there, or (setting) the window moves to the current desktop.
void LinkRoutingController::BringToFront(BrowserWindow& window) {
  HWND hwnd = window.hwnd();
  if (!OnCurrentDesktop(hwnd) && host_.MoveChosenWindowHere()) {
    const Microsoft::WRL::ComPtr<IVirtualDesktopManager> manager =
        DesktopManager();
    GUID desktop = {};
    HWND here = GetForegroundWindow();
    if (manager && here &&
        SUCCEEDED(manager->GetWindowDesktopId(here, &desktop))) {
      manager->MoveWindowToDesktop(hwnd, desktop);
    }
  }
  window.Activate();
}

}  // namespace brouk::win
