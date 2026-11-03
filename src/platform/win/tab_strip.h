#ifndef BROUK_PLATFORM_WIN_TAB_STRIP_H_
#define BROUK_PLATFORM_WIN_TAB_STRIP_H_

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>
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
    kTabAudio,  // The speaker of a playing or muted tab: mutes or unmutes.
    kNewTab,    // A profile group's "+".
    kGroupLabel,
    kMinimize,
    kMaximize,
    kClose,
  };
  struct Hit {
    Part part = Part::kNone;
    TabId tab = 0;           // For kTab, kTabClose and kTabAudio.
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

  // How the strip finds a tab's state (audio) for hit tests.
  void SetTabLookup(std::function<const Tab*(TabId)> tab_of) {
    tab_of_ = std::move(tab_of);
  }
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

  // Where tabs of `profile` dropped at `x` go (026), not counting
  // `excluded` tabs (dragged from this strip) and, for a dragged group
  // (`group`), the group itself.
  struct DropPlace {
    // Position inside the profile's group.
    std::size_t position = 0;
    // The group's place among the groups if it is created or moved.
    std::size_t group_index = 0;
    // Dropped inside another profile's group (a single tab: asks to move
    // it to that profile, 005).
    std::optional<ProfileId> other_profile;
    int mark_x = 0;  // Where the insertion mark goes.
  };
  DropPlace DropPlaceAt(int x,
                        const ProfileId& profile,
                        const TabStripModel& model,
                        const std::vector<TabId>& excluded,
                        bool group) const;
  // Whether `point` is on the strip or a little above or below it, where
  // dragged tabs attach.
  bool InDropArea(POINT point) const;
  // How far above or below the strip a dragged tab leaves it.
  int DetachDistance() const { return Scale(12); }
  // Where a group's label is, for the image of a dragged group.
  std::optional<RECT> GroupLabelRect(const ProfileId& profile) const;
  // Whether `profile`'s group is the last one: the default profile's,
  // which does not move within the strip.
  bool IsLastGroup(const ProfileId& profile) const;

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
    // Tabs dragged over the strip: where they would go (026).
    std::optional<int> drop_mark;
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
  bool HasClose(const TabGeometry& tab) const;
  // The speaker left of the close button, when the tab plays or is muted
  // (as Chrome); empty otherwise.
  RECT AudioRect(const TabGeometry& tab, const Tab* state) const;
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
  std::function<const Tab*(TabId)> tab_of_;
  HFONT title_font_ = nullptr;
  HFONT label_font_ = nullptr;
  HFONT glyph_font_ = nullptr;
  HFONT small_glyph_font_ = nullptr;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_TAB_STRIP_H_
