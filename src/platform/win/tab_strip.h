#ifndef BROUK_PLATFORM_WIN_TAB_STRIP_H_
#define BROUK_PLATFORM_WIN_TAB_STRIP_H_

#include <windows.h>

#include <functional>
#include <optional>
#include <vector>

#include "core/tab_strip_model.h"
#include "platform/win/theme.h"

namespace brouk::win {

class Tab;

// The title bar of a browser window, drawn by Brouk (024: tabs in the title
// bar, like Chrome and Edge): the tabs, the new tab button and the window's
// minimise, maximise and close buttons. It computes where everything is
// (with the core's LayoutTabStrip), answers what is under a point, and
// paints; the window handles the input. UI thread.
class TabStrip {
 public:
  enum class Part {
    kNone,  // Empty title bar: drags the window.
    kTab,
    kTabClose,  // A tab's close button.
    kNewTab,
    kMinimize,
    kMaximize,
    kClose,
  };
  struct Hit {
    Part part = Part::kNone;
    TabId tab = 0;  // For kTab and kTabClose.
    bool operator==(const Hit&) const = default;
  };

  TabStrip() = default;
  ~TabStrip();
  TabStrip(const TabStrip&) = delete;
  TabStrip& operator=(const TabStrip&) = delete;

  // Lays the strip out in `bounds` (client coordinates) at `dpi`.
  // `caption_buttons`: false for windows with a system title bar.
  void Layout(const RECT& bounds,
              UINT dpi,
              const TabStripModel& model,
              bool caption_buttons);
  Hit HitTest(POINT point) const;
  // The tab at `x` (the first or last beyond the ends): where a dragged tab
  // goes.
  std::optional<TabId> TabAtX(int x) const;
  std::optional<RECT> TabRect(TabId tab) const;

  struct PaintState {
    const Theme* theme = nullptr;
    std::function<const Tab*(TabId)> tab;
    std::optional<TabId> active;
    Hit hover;
    Hit pressed;
    bool window_active = true;
    bool maximized = false;
    int spinner_step = 0;  // Animation of loading tabs.
  };
  void Paint(HDC dc, const PaintState& state) const;

 private:
  struct TabGeometry {
    TabId id = 0;
    RECT rect = {};
    RECT close = {};
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

  RECT bounds_ = {};
  UINT dpi_ = 0;
  std::vector<TabGeometry> tabs_;
  RECT new_tab_ = {};
  RECT minimize_ = {};
  RECT maximize_ = {};
  RECT close_ = {};
  bool caption_buttons_ = true;
  HFONT title_font_ = nullptr;
  HFONT glyph_font_ = nullptr;
  HFONT small_glyph_font_ = nullptr;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_TAB_STRIP_H_
