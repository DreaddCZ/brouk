#ifndef BROUK_PLATFORM_WIN_TAB_PANEL_H_
#define BROUK_PLATFORM_WIN_TAB_PANEL_H_

#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "platform/win/theme.h"

namespace brouk::win {

// A panel Brouk draws over one tab's page (024): the tab-modal dialogs of
// 012 (JavaScript dialogs, "page unresponsive", "open <app>?"), the
// permission prompt and the crash page. A child window of the tab's
// container, so it moves, hides and closes with its tab and other tabs stay
// usable, as in Chrome. The tab decides what the panel blocks. UI thread.
class TabPanel {
 public:
  enum class Placement {
    kTopCenter,  // Dialogs, near the top of the page (Chrome).
    kTopLeft,    // The permission prompt, under the address bar.
    kFill,       // Covers the page (the crash page).
  };
  // A line with an icon (Segoe MDL2 Assets glyph) and its text.
  struct Line {
    std::wstring glyph;
    std::wstring text;
  };
  struct Content {
    Placement placement = Placement::kTopCenter;
    int width = 440;     // In DIPs; for kFill, the column of text.
    std::wstring icon;   // A large glyph above the title.
    std::wstring title;  // Also the panel's name for screen readers.
    std::vector<Line> lines;
    std::wstring message;  // Selectable, scrolls when long (not for kFill).
    std::wstring detail;   // Small, dim text under the message.
    // A text field with this text (window.prompt()), named for screen
    // readers by `input_name`.
    std::optional<std::wstring> input;
    std::wstring input_name;
    std::wstring checkbox;  // A checkbox with this label, if not empty.
    std::vector<std::wstring> buttons;
    // The first button is the default one: highlighted, and what Enter
    // answers outside the buttons.
    bool first_is_default = true;
    bool stacked_buttons = false;  // One per line, full width.
    bool close_button = false;     // An "X" at the top right (kClosed).
    // What Escape answers: a button's index or kClosed; nothing if empty.
    std::optional<int> escape_answer;
  };
  static constexpr int kClosed = -1;
  struct Answer {
    int button = kClosed;
    std::wstring input;
    bool checked = false;
  };
  using Done = std::function<void(const Answer& answer)>;
  // Browser shortcuts typed in the panel (Ctrl+T, ...); true if handled.
  using Shortcuts =
      std::function<bool(int key, bool control, bool alt, bool shift)>;

  // Shows the panel in `parent` (the tab's container), not focused. `done`
  // is called once with the answer, from a task after the panel has hidden
  // itself, so it may delete the panel; not at all if the panel is deleted
  // before the user answers.
  TabPanel(HWND parent,
           const Theme& theme,
           Content content,
           Done done,
           Shortcuts shortcuts);
  ~TabPanel();
  TabPanel(const TabPanel&) = delete;
  TabPanel& operator=(const TabPanel&) = delete;

  // Places the panel again after the parent's size changed.
  void Place();
  // Gives keyboard focus to the text field or the default button.
  void Focus();
  // Answers as Escape does (or as the close button), for the user.
  void Cancel();
  // Whether the user has answered (the panel is hidden then).
  bool answered() const { return answered_; }

 private:
  struct Glyph {
    RECT rect;
    std::wstring text;
    HFONT font;
    COLORREF color;
  };

  static LRESULT CALLBACK PanelProc(HWND window,
                                    UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam);
  static LRESULT CALLBACK ControlProc(HWND window,
                                      UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam,
                                      UINT_PTR id,
                                      DWORD_PTR data);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

  void CreateFonts();
  void CreateControls();
  HWND AddControl(const wchar_t* control_class,
                  const std::wstring& text,
                  DWORD style,
                  int id,
                  HFONT font);
  // Moves the controls to a column `width` wide at (`x`, `y`) when
  // `apply`; returns the column's height.
  int Layout(int x, int y, int width, bool apply);
  void Paint();
  void DrawButton(const DRAWITEMSTRUCT& item);
  void MoveFocus(bool forward);
  void Finish(int button);
  int Scale(int dips) const;

  HWND parent_;
  Theme theme_;
  const Content content_;
  Done done_;
  Shortcuts shortcuts_;
  bool answered_ = false;
  UINT dpi_ = 96;

  HWND window_ = nullptr;
  HWND title_ = nullptr;
  std::vector<HWND> line_labels_;
  HWND message_ = nullptr;
  HWND detail_ = nullptr;
  HWND input_ = nullptr;
  HWND checkbox_ = nullptr;
  HWND checkbox_label_ = nullptr;
  HWND close_ = nullptr;
  std::vector<HWND> buttons_;
  // Keyboard focus order (Tab and Shift+Tab).
  std::vector<HWND> focus_order_;
  std::vector<Glyph> glyphs_;
  int height_ = 0;  // Of a panel that does not fill its parent.

  HFONT text_font_ = nullptr;
  HFONT title_font_ = nullptr;
  HFONT small_font_ = nullptr;
  HFONT glyph_font_ = nullptr;
  HFONT small_glyph_font_ = nullptr;
  HFONT icon_font_ = nullptr;
  HBRUSH background_brush_ = nullptr;
  HBRUSH field_brush_ = nullptr;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_TAB_PANEL_H_
