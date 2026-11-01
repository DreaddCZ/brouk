#ifndef BROUK_PLATFORM_WIN_LINK_ROUTING_H_
#define BROUK_PLATFORM_WIN_LINK_ROUTING_H_

#include <windows.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/link_router.h"
#include "core/link_routing_rules.h"
#include "core/profile_registry.h"
#include "core/result.h"
#include "core/tab_strip_model.h"
#include "platform/win/link_picker.h"
#include "platform/win/link_resolver.h"

namespace brouk::win {

class BrowserWindow;

// What link routing needs from the app (implemented by App).
class LinkRoutingHost {
 public:
  virtual ProfileId RoutingDefaultProfile() = 0;
  virtual const std::vector<ProfileInfo>& RoutingProfiles() = 0;
  virtual COLORREF RoutingProfileColor(const ProfileId& profile) = 0;
  // The normal browser windows, front first (minimised ones last).
  virtual std::vector<BrowserWindow*> RoutingWindows() = 0;
  virtual bool IsRoutingWindow(const BrowserWindow* window) = 0;
  // A new, empty browser window.
  virtual BrowserWindow* OpenRoutingWindow() = 0;
  // "When the chosen window is on another desktop": move it here.
  virtual bool MoveChosenWindowHere() = 0;
  // Browsers resolving opaque links opened (+1) or closed (-1).
  virtual void RoutingBrowserCount(int delta) = 0;

 protected:
  ~LinkRoutingHost() = default;
};

// Link routing (010) on Windows, as on macOS: takes a link from another app
// or a new tab from inside the browser, looks through link wrappers, asks
// DecideProfile and DecideWindow where it belongs, shows the picker when
// the user has to choose, and opens it there. Owns the rules file
// (link-routing.yaml). Windows take the place of macOS Spaces' virtual
// desktops (IVirtualDesktopManager). UI thread.
class LinkRoutingController {
 public:
  LinkRoutingController(LinkRoutingHost& host,
                        std::filesystem::path config_file);
  ~LinkRoutingController();
  LinkRoutingController(const LinkRoutingController&) = delete;
  LinkRoutingController& operator=(const LinkRoutingController&) = delete;

  // Reads the rules file again (it changed, or at start).
  void Reload();
  const LinkRoutingConfig& config() const { return config_; }
  const std::vector<std::string>& problems() const { return problems_; }
  // Saves new rules (settings page).
  std::optional<Error> SetConfig(LinkRoutingConfig config);

  // A link from another app. Links that arrive while the picker is open
  // wait for it.
  void OpenExternalUrl(const std::string& url);
  // A new tab or window from inside the browser (`source` kNewTab or
  // kTypedNewTab; `tab` is the opener, or the new tab the address was
  // typed into). False when the link stays in the tab's profile and the
  // caller opens it as before; true when routing took it over.
  bool RouteUrl(const std::string& url,
                LinkSource source,
                BrowserWindow& window,
                TabId tab,
                bool foreground);
  // A page whose profile is already known, opened from an internal page in
  // a tab of another profile (the history page, a linked bookmark). Only
  // the window is chosen (the picker's window step when the profile's group
  // is in several windows). `source_name` is shown ("From History").
  void OpenUrlInProfile(const std::string& url,
                        const ProfileId& profile,
                        const std::wstring& source_name,
                        BrowserWindow* window,
                        bool foreground,
                        bool bookmark);
  // Quitting: closes the picker and stops resolving links.
  void CancelPending();

 private:
  // One link on its way to a tab.
  struct Request {
    std::string url;
    LinkSource source = LinkSource::kOtherApp;
    ProfileId source_profile;
    std::wstring source_name;         // "From another app" / "From Work".
    BrowserWindow* window = nullptr;  // Checked with IsRoutingWindow.
    TabId tab = 0;
    bool foreground = true;
    HMONITOR monitor = nullptr;
    // Set once the real URL is known: what routing uses, what the tab
    // loads (the wrapper, so the company's check runs), and the wrapper.
    std::optional<std::string> real_url;
    std::string open_url;
    std::string wrapper;
    // Set when the profile is already known (OpenUrlInProfile).
    ProfileId known_profile;
    bool bookmark = false;
  };

  void Enqueue(Request request);
  void EnqueueResolved(Request request);
  void ProcessNext();
  void Process(Request request);
  void Route(Request request);
  ProfileDecision DecideProfileFor(const Request& request,
                                   const std::string& real_url);
  WindowDecision DecideWindowFor(const ProfileId& profile,
                                 const Request& request);
  void ShowPicker(Request request, const ProfileDecision& decision);
  std::vector<LinkPickerWindow> PickerWindowsFor(const ProfileId& profile,
                                                 const Request& request);
  void PickerDone(const LinkPickerResult& result,
                  const Request& request,
                  const std::optional<std::string>& pattern);
  void OpenInSource(const Request& request);
  void Open(const Request& request,
            const ProfileId& profile,
            const WindowDecision& windows);
  void BringToFront(BrowserWindow& window);
  BrowserWindow* WindowWithId(std::int64_t id);
  // On the virtual desktop the user is on (Windows 10 and 11).
  bool OnCurrentDesktop(HWND window);
  void LogRouting(const Request& request, const ProfileDecision& decision);

  LinkRoutingHost& host_;
  std::filesystem::path path_;
  LinkRoutingConfig config_;
  std::vector<std::string> problems_;
  LinkPicker picker_;
  LinkResolver resolver_;
  // Links waiting while the picker is open.
  std::deque<Request> queue_;
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_LINK_ROUTING_H_
