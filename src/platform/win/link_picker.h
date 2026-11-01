#ifndef BROUK_PLATFORM_WIN_LINK_PICKER_H_
#define BROUK_PLATFORM_WIN_LINK_PICKER_H_

#include <windows.h>

#include <dwmapi.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/profile_id.h"
#include "platform/win/theme.h"

namespace brouk::win {

// A profile the picker offers, in its colour.
struct LinkPickerProfile {
  ProfileId id;
  std::wstring name;
  COLORREF color = 0;
};

// One profile group of a window, as a chip ("Work 5").
struct LinkPickerGroup {
  std::wstring name;
  COLORREF color = 0;
  std::size_t tabs = 0;
};

// A window the picker offers: its title (of its active tab), its groups,
// and the window itself for a live thumbnail (DWM).
struct LinkPickerWindow {
  std::int64_t id = 0;
  HWND window = nullptr;
  std::wstring title;
  std::vector<LinkPickerGroup> groups;
};

// What the user chose.
struct LinkPickerResult {
  bool cancelled = false;
  ProfileId profile;
  // "Always open links matching … in this profile" was checked.
  bool always = false;
  enum class Window {
    kNotAsked,   // No window step: routing decides.
    kExisting,   // `window_id`.
    kNewWindow,  // "New window".
  };
  Window window = Window::kNotAsked;
  std::int64_t window_id = 0;
};

// The link picker (010), as on macOS: one dialog that chooses the profile
// (when a rule lists several) and then the window (when the profile's group
// is in several windows) for a link. It only shows choices and reports the
// answer; DecideProfile and DecideWindow decide when it is needed. Number
// keys choose at once, arrows plus Enter also work, A ticks "Always…",
// Escape cancels (or goes back from the window step, as Left does). The
// windows show live thumbnails (DWM), as the taskbar does. UI thread.
class LinkPicker {
 public:
  using WindowsFor =
      std::function<std::vector<LinkPickerWindow>(const ProfileId& profile)>;
  using Done = std::function<void(const LinkPickerResult& result)>;

  LinkPicker();
  ~LinkPicker();
  LinkPicker(const LinkPicker&) = delete;
  LinkPicker& operator=(const LinkPicker&) = delete;

  // Shows the picker centred on `monitor`, in front of all windows.
  //   `profiles`: more than one shows the profile step first; one skips it.
  //   `rule_pattern`: for the "Always open…" checkbox (none: no checkbox).
  //   `windows_for`: the windows to offer for the chosen profile, most
  //   recently used first; empty skips the window step.
  //   `done`: called once, after the picker has closed.
  void Show(const std::wstring& url,
            const std::wstring& via,
            const std::wstring& source,
            std::vector<LinkPickerProfile> profiles,
            std::optional<std::wstring> rule_pattern,
            WindowsFor windows_for,
            HMONITOR monitor,
            Done done);
  // Closes it as if the user cancelled.
  void Cancel();
  bool visible() const { return window_ && IsWindowVisible(window_); }

 private:
  // A row's thumbnail, registered while the window step is shown.
  struct Thumbnail {
    HTHUMBNAIL handle = nullptr;
    SIZE source = {};
  };

  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void ShowProfileStep(int selected);
  // False when the chosen profile needs no window step.
  bool ShowWindowStep();
  void Layout();
  void Paint();
  int RowCount() const;
  int RowHeight() const;
  int VisibleRows() const;
  RECT RowRect(int row) const;
  int RowAt(POINT point) const;
  void Select(int row);
  void Choose(int row);
  void Back();
  void Finish(const LinkPickerResult& result);
  void UpdateThumbnails();
  void ClearThumbnails();
  void UpdateFonts();
  int Scale(int dips) const;

  HWND window_ = nullptr;
  Theme theme_;
  HFONT title_font_ = nullptr;
  HFONT text_font_ = nullptr;
  HFONT small_font_ = nullptr;
  HFONT icon_font_ = nullptr;
  std::wstring url_;
  std::wstring source_;
  std::vector<LinkPickerProfile> profiles_;
  std::optional<std::wstring> rule_pattern_;
  WindowsFor windows_for_;
  Done done_;
  bool window_step_ = false;
  ProfileId chosen_;
  std::vector<LinkPickerWindow> windows_;
  std::vector<Thumbnail> thumbnails_;
  bool always_ = false;
  int selected_ = 0;
  int first_row_ = 0;  // Scrolled: the first row shown.
  // Clickable parts besides the rows.
  RECT always_rect_ = {};
  RECT back_rect_ = {};
  RECT cancel_rect_ = {};
  int list_top_ = 0;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_LINK_PICKER_H_
