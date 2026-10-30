#ifndef BROUK_PLATFORM_WIN_TAB_STRIP_H_
#define BROUK_PLATFORM_WIN_TAB_STRIP_H_

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/tab_strip_model.h"
#include "platform/win/theme.h"

namespace brouk::win {

class Tab;

// The title bar of a browser window, drawn by Brouk (024: tabs in the title
// bar, like Chrome and Edge): one group per profile (003, 005) with its
// label, tabs and "+" button, and the window's minimise, maximise and close
// buttons. It computes where everything is
// (with the core's LayoutTabStrip), answers what is under a point, and
// paints; the window handles the input. UI thread.
class TabStrip {
 public:
  enum class Part {
    kNone,  // Empty title bar: drags the window.
    kTab,
    kTabClose,  // A tab's close button.
    kNewTab,    // A profile group's "+".
    kGroupLabel,
    kMinimize,
    kMaximize,
    kClose,
  };
  struct Hit {
    Part part = Part::kNone;
    TabId tab = 0;           // For kTab and kTabClose.
    ProfileId profile = {};  // For kNewTab and kGroupLabel.
    bool operator==(const Hit&) const = default;
  };

  TabStrip() = default;
  ~TabStrip();
  TabStrip(const TabStrip&) = delete;
  TabStrip& operator=(const TabStrip&) = delete;

  // How a profile's group looks: its name and colour (one of
  // ProfileColorNames()).
  struct GroupLook {
    std::string name;
    std::string color;
  };
  using LookOf = std::function<GroupLook(const ProfileId& profile)>;

  // Lays the strip out in `bounds` (client coordinates) at `dpi`.
  // `caption_buttons`: false for windows with a system title bar.
  void Layout(const RECT& bounds,
              UINT dpi,
              const TabStripModel& model,
              bool caption_buttons,
              const LookOf& look);
  // Whether the group of `profile` is shown collapsed because there is no
  // room (clicking its label then shows its tabs instead).
  bool IsAutoCollapsed(const ProfileId& profile) const;
  // A tooltip for the part (group labels and "+"), or empty.
  std::wstring ToolTip(const Hit& hit) const;
  Hit HitTest(POINT point) const;
  // The tab at `x` (the first or last beyond the ends): where a dragged tab
  // goes.
  std::optional<TabId> TabAtX(int x) const;
  // The profile whose group (label, tabs and "+") is at `point`: where a
  // dragged tab is dropped to move it to another profile (005).
  std::optional<ProfileId> GroupAt(POINT point) const;
  std::optional<RECT> TabRect(TabId tab) const;

  struct PaintState {
    const Theme* theme = nullptr;
    std::function<const Tab*(TabId)> tab;
    std::optional<TabId> active;
    Hit hover;
    Hit pressed;
    bool window_active = true;
    bool maximized = false;
    bool dark = false;     // Profile colours have a dark-mode shade.
    int spinner_step = 0;  // Animation of loading tabs.
  };
  void Paint(HDC dc, const PaintState& state) const;

 private:
  struct TabGeometry {
    TabId id = 0;
    RECT rect = {};
    RECT close = {};
  };
  struct GroupGeometry {
    ProfileId profile;
    std::wstring name;
    std::string color;
    std::size_t count = 0;
    bool collapsed = false;       // By the user.
    bool auto_collapsed = false;  // For lack of room.
    RECT label = {};
    RECT plus = {};
    RECT line = {};  // Under the group's tabs; empty if none are shown.
  };
  int Scale(int dips) const;
  void UpdateFonts();
  void PaintTab(HDC dc,
                const TabGeometry& geometry,
                const PaintState& state) const;
  void PaintButton(HDC dc,
                   const RECT& rect,
                   const wchar_t* glyph,
                   HFONT font,
                   COLORREF background,
                   COLORREF text) const;
  void PaintGroup(HDC dc,
                  const GroupGeometry& group,
                  const PaintState& state) const;

  RECT bounds_ = {};
  UINT dpi_ = 0;
  std::vector<TabGeometry> tabs_;
  std::vector<GroupGeometry> groups_;
  RECT minimize_ = {};
  RECT maximize_ = {};
  RECT close_ = {};
  bool caption_buttons_ = true;
  HFONT title_font_ = nullptr;
  HFONT label_font_ = nullptr;
  HFONT glyph_font_ = nullptr;
  HFONT small_glyph_font_ = nullptr;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_TAB_STRIP_H_
