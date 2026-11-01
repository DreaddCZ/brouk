#ifndef BROUK_PLATFORM_WIN_COMMAND_PALETTE_H_
#define BROUK_PLATFORM_WIN_COMMAND_PALETTE_H_

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "core/command_palette.h"
#include "platform/win/theme.h"

namespace brouk::win {

// One thing the palette can run.
struct PaletteEntry {
  PaletteItem::Kind kind = PaletteItem::Kind::kAction;
  std::string title;
  std::string detail;
  std::wstring shortcut;  // Shown at the right, e.g. "Ctrl+Shift+T".
  // Runs after the palette has closed.
  std::function<void()> run;
};

// The command palette (003), as on macOS: a search field over a list of
// tabs, actions, bookmarks, settings and help sections, at the top of a
// browser window. Ranking comes from RankPaletteItems; this class shows the
// list and runs the chosen entry. Up/Down move, Enter runs, Escape or
// clicking elsewhere closes it. UI thread.
class CommandPalette {
 public:
  CommandPalette();
  ~CommandPalette();
  CommandPalette(const CommandPalette&) = delete;
  CommandPalette& operator=(const CommandPalette&) = delete;

  // Shows the palette over `owner` (centred near its top, `top` pixels
  // below the window's top) with an empty query. The fonts are the
  // window's, which outlive the palette.
  void Show(HWND owner,
            int top,
            std::vector<PaletteEntry> entries,
            const Theme& theme,
            HFONT text_font,
            HFONT icon_font);
  void Close();
  bool visible() const { return window_ && IsWindowVisible(window_); }

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  static LRESULT CALLBACK FieldProc(HWND window,
                                    UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam,
                                    UINT_PTR id,
                                    DWORD_PTR data);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  // Ranks the entries for the field's text and resizes the window.
  void Filter();
  void Paint();
  void Select(int row);
  void Run(int row);
  int Scale(int dips) const;
  int RowHeight() const;
  int FieldHeight() const;
  int RowAt(int y) const;

  HWND window_ = nullptr;
  HWND field_ = nullptr;
  HWND owner_ = nullptr;
  std::vector<PaletteEntry> entries_;
  std::vector<PaletteItem> items_;
  // Indices into entries_, best first.
  std::vector<std::size_t> shown_;
  int selected_ = 0;
  Theme theme_;
  HFONT text_font_ = nullptr;
  HFONT icon_font_ = nullptr;
  HBRUSH field_brush_ = nullptr;
  int width_ = 0;
  int top_ = 0;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_COMMAND_PALETTE_H_
