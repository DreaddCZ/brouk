#include "platform/win/tab_strip.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "core/tab_strip_layout.h"
#include "platform/win/tab.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

// Sizes in device-independent pixels (96 DPI), close to Chrome's on
// Windows 11.
constexpr int kCaptionButtonWidth = 46;
constexpr int kTabTopMargin = 6;
constexpr int kStripLeftMargin = 8;
constexpr int kNewTabButtonSize = 28;
constexpr int kNewTabGap = 4;
// Empty title bar always left for dragging the window.
constexpr int kMinDragWidth = 48;
constexpr int kTabMaxWidth = 240;
constexpr int kTabMinWidth = 44;
constexpr int kTabPadding = 10;
constexpr int kIconSize = 16;
constexpr int kCloseButtonSize = 20;
constexpr int kTabCornerRadius = 8;

// Glyphs of the "Segoe MDL2 Assets" icon font (Windows 10 and 11).
constexpr wchar_t kMinimizeGlyph[] = L"";
constexpr wchar_t kMaximizeGlyph[] = L"";
constexpr wchar_t kRestoreGlyph[] = L"";
constexpr wchar_t kCloseGlyph[] = L"";
constexpr wchar_t kAddGlyph[] = L"";
constexpr wchar_t kPageGlyph[] = L"";  // Globe: a page without icon.

bool Contains(const RECT& rect, POINT point) {
  return PtInRect(&rect, point) != FALSE;
}

void Fill(HDC dc, const RECT& rect, COLORREF color) {
  SetDCBrushColor(dc, color);
  FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

HFONT GlyphFont(UINT dpi, int dips) {
  LOGFONTW font = {};
  font.lfHeight = -MulDiv(dips, static_cast<int>(dpi), 96);
  font.lfWeight = FW_NORMAL;
  font.lfCharSet = DEFAULT_CHARSET;
  font.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(font.lfFaceName, L"Segoe MDL2 Assets");
  return CreateFontIndirectW(&font);
}

}  // namespace

TabStrip::~TabStrip() {
  for (HFONT font : {title_font_, glyph_font_, small_glyph_font_}) {
    if (font) {
      DeleteObject(font);
    }
  }
}

int TabStrip::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(dpi_), 96);
}

void TabStrip::UpdateFonts() {
  for (HFONT* font : {&title_font_, &glyph_font_, &small_glyph_font_}) {
    if (*font) {
      DeleteObject(*font);
      *font = nullptr;
    }
  }
  NONCLIENTMETRICSW metrics = {};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                             0, dpi_);
  title_font_ = CreateFontIndirectW(&metrics.lfMessageFont);
  glyph_font_ = GlyphFont(dpi_, 10);
  small_glyph_font_ = GlyphFont(dpi_, 8);
}

void TabStrip::Layout(const RECT& bounds,
                      UINT dpi,
                      const TabStripModel& model,
                      bool caption_buttons) {
  if (dpi != dpi_ || !title_font_) {
    dpi_ = dpi;
    UpdateFonts();
  }
  bounds_ = bounds;
  caption_buttons_ = caption_buttons;

  int right = bounds.right;
  if (caption_buttons) {
    const int width = Scale(kCaptionButtonWidth);
    close_ = {right - width, bounds.top, right, bounds.bottom};
    maximize_ = {close_.left - width, bounds.top, close_.left, bounds.bottom};
    minimize_ = {maximize_.left - width, bounds.top, maximize_.left,
                 bounds.bottom};
    right = minimize_.left;
  } else {
    close_ = maximize_ = minimize_ = {};
  }

  const int tab_top = bounds.top + Scale(kTabTopMargin);
  const int left = bounds.left + Scale(kStripLeftMargin);
  const int reserved =
      Scale(kNewTabGap + kNewTabButtonSize + kNewTabGap + kMinDragWidth);

  // One layout group per profile group; W1 has no group labels yet.
  TabStripLayoutInput input;
  input.width = std::max(0, right - left - reserved);
  input.metrics.max_tab_width = Scale(kTabMaxWidth);
  input.metrics.min_tab_width = Scale(kTabMinWidth);
  input.metrics.pinned_min_width = Scale(96);
  input.metrics.tab_gap = 0;
  input.metrics.more_button_width = Scale(40);
  const std::vector<TabStripModel::Group> groups = model.groups();
  const std::optional<std::size_t> active = model.active_index();
  for (std::size_t g = 0; g < groups.size(); ++g) {
    const TabStripModel::Group& group = groups[g];
    input.groups.push_back({.fixed_width = 0,
                            .tab_count = group.count,
                            .pinned_count = group.pinned_count,
                            .collapsed = group.collapsed});
    if (active && *active >= group.start &&
        *active < group.start + group.count) {
      input.active_group = g;
      input.active_tab = *active - group.start;
    }
  }
  const std::vector<TabStripGroupLayout> layout = LayoutTabStrip(input);

  tabs_.clear();
  double x = left;
  for (std::size_t g = 0; g < groups.size() && g < layout.size(); ++g) {
    const TabStripGroupLayout& group_layout = layout[g];
    for (std::size_t i = 0; i < group_layout.tab_widths.size(); ++i) {
      const double width = group_layout.tab_widths[i];
      if (width <= 0) {
        continue;
      }
      TabGeometry geometry;
      geometry.id = model.id_at(groups[g].start + i);
      geometry.rect = {static_cast<int>(std::lround(x)), tab_top,
                       static_cast<int>(std::lround(x + width)), bounds.bottom};
      const int close_size = Scale(kCloseButtonSize);
      const int close_right = geometry.rect.right - Scale(kTabPadding) / 2;
      const int close_top =
          tab_top + (bounds.bottom - tab_top - close_size) / 2;
      geometry.close = {close_right - close_size, close_top, close_right,
                        close_top + close_size};
      tabs_.push_back(geometry);
      x += width;
    }
  }
  const int button = Scale(kNewTabButtonSize);
  const int new_tab_left = static_cast<int>(std::lround(x)) + Scale(kNewTabGap);
  const int new_tab_top = tab_top + (bounds.bottom - tab_top - button) / 2;
  new_tab_ = {new_tab_left, new_tab_top, new_tab_left + button,
              new_tab_top + button};
}

TabStrip::Hit TabStrip::HitTest(POINT point) const {
  if (!Contains(bounds_, point)) {
    return {};
  }
  if (caption_buttons_) {
    if (Contains(close_, point)) {
      return {Part::kClose, 0};
    }
    if (Contains(maximize_, point)) {
      return {Part::kMaximize, 0};
    }
    if (Contains(minimize_, point)) {
      return {Part::kMinimize, 0};
    }
  }
  for (const TabGeometry& tab : tabs_) {
    if (Contains(tab.rect, point)) {
      // Very narrow tabs show only their icon and no close button.
      const bool has_close = tab.rect.right - tab.rect.left >=
                             Scale(kTabPadding + kIconSize + kCloseButtonSize);
      if (has_close && Contains(tab.close, point)) {
        return {Part::kTabClose, tab.id};
      }
      return {Part::kTab, tab.id};
    }
  }
  if (Contains(new_tab_, point)) {
    return {Part::kNewTab, 0};
  }
  return {};
}

std::optional<TabId> TabStrip::TabAtX(int x) const {
  if (tabs_.empty()) {
    return std::nullopt;
  }
  for (const TabGeometry& tab : tabs_) {
    if (x < tab.rect.right) {
      return tab.id;
    }
  }
  return tabs_.back().id;
}

std::optional<RECT> TabStrip::TabRect(TabId tab) const {
  for (const TabGeometry& geometry : tabs_) {
    if (geometry.id == tab) {
      return geometry.rect;
    }
  }
  return std::nullopt;
}

void TabStrip::PaintButton(HDC dc,
                           const RECT& rect,
                           const wchar_t* glyph,
                           HFONT font,
                           COLORREF background,
                           COLORREF text) const {
  Fill(dc, rect, background);
  HGDIOBJ previous = SelectObject(dc, font);
  SetTextColor(dc, text);
  RECT area = rect;
  DrawTextW(dc, glyph, -1, &area,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, previous);
}

void TabStrip::PaintTab(HDC dc,
                        const TabGeometry& geometry,
                        const PaintState& state) const {
  const Theme& theme = *state.theme;
  const Tab* tab = state.tab(geometry.id);
  const bool active = state.active == geometry.id;
  const bool hovered =
      state.hover.tab == geometry.id &&
      (state.hover.part == Part::kTab || state.hover.part == Part::kTabClose);
  const RECT& rect = geometry.rect;

  if (active || hovered) {
    // Rounded top corners; the bottom joins the toolbar.
    const COLORREF fill = active ? theme.toolbar : theme.hover;
    SetDCBrushColor(dc, fill);
    SetDCPenColor(dc, fill);
    HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
    const int radius = Scale(kTabCornerRadius);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom + radius,
              radius * 2, radius * 2);
    SelectObject(dc, brush);
    SelectObject(dc, pen);
  }

  const int padding = Scale(kTabPadding);
  const int icon = Scale(kIconSize);
  const int icon_x = rect.left + padding;
  const int icon_y = rect.top + (rect.bottom - rect.top - icon) / 2;
  if (tab && tab->loading()) {
    // A turning arc, as a throbber.
    const double start =
        state.spinner_step * std::numbers::pi / 6.0;  // 30° per step.
    const double end = start + std::numbers::pi * 1.5;
    const int radius = icon / 2 - Scale(1);
    const int cx = icon_x + icon / 2;
    const int cy = icon_y + icon / 2;
    HPEN pen = CreatePen(PS_SOLID, std::max(1, Scale(2)),
                         GetSysColor(COLOR_HIGHLIGHT));
    HGDIOBJ previous_pen = SelectObject(dc, pen);
    HGDIOBJ previous_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Arc(dc, cx - radius, cy - radius, cx + radius, cy + radius,
        cx + static_cast<int>(radius * std::cos(start)),
        cy - static_cast<int>(radius * std::sin(start)),
        cx + static_cast<int>(radius * std::cos(end)),
        cy - static_cast<int>(radius * std::sin(end)));
    SelectObject(dc, previous_brush);
    SelectObject(dc, previous_pen);
    DeleteObject(pen);
  } else if (tab && !tab->favicon().empty()) {
    tab->favicon().Draw(dc, icon_x, icon_y);
  } else {
    RECT glyph = {icon_x, icon_y, icon_x + icon, icon_y + icon};
    HGDIOBJ previous = SelectObject(dc, glyph_font_);
    SetTextColor(dc, theme.text_dim);
    DrawTextW(dc, kPageGlyph, -1, &glyph,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
  }

  const bool has_close = rect.right - rect.left >=
                         Scale(kTabPadding + kIconSize + kCloseButtonSize);
  RECT title = {
      icon_x + icon + Scale(8), rect.top,
      has_close ? geometry.close.left - Scale(4) : rect.right - padding,
      rect.bottom};
  if (title.right > title.left) {
    std::wstring text =
        tab && !tab->title().empty() ? Utf8ToWide(tab->title()) : L"New Tab";
    HGDIOBJ previous = SelectObject(dc, title_font_);
    SetTextColor(dc, active ? theme.text : theme.text_dim);
    DrawTextW(
        dc, text.c_str(), static_cast<int>(text.size()), &title,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, previous);
  }

  if (has_close) {
    const bool close_hovered =
        state.hover.part == Part::kTabClose && state.hover.tab == geometry.id;
    if (close_hovered) {
      SetDCBrushColor(dc, active ? theme.hover : theme.separator);
      SetDCPenColor(dc, active ? theme.hover : theme.separator);
      HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
      HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
      Ellipse(dc, geometry.close.left, geometry.close.top, geometry.close.right,
              geometry.close.bottom);
      SelectObject(dc, brush);
      SelectObject(dc, pen);
    }
    RECT close = geometry.close;
    HGDIOBJ previous = SelectObject(dc, small_glyph_font_);
    SetTextColor(dc, theme.text);
    DrawTextW(dc, kCloseGlyph, -1, &close,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
  }
}

void TabStrip::Paint(HDC dc, const PaintState& state) const {
  const Theme& theme = *state.theme;
  const COLORREF frame =
      state.window_active ? theme.frame : theme.frame_inactive;
  Fill(dc, bounds_, frame);
  SetBkMode(dc, TRANSPARENT);

  // Separators between inactive, not hovered tabs (Chrome).
  const auto plain = [&](TabId id) {
    return state.active != id &&
           !(state.hover.tab == id && (state.hover.part == Part::kTab ||
                                       state.hover.part == Part::kTabClose));
  };
  for (std::size_t i = 0; i + 1 < tabs_.size(); ++i) {
    if (plain(tabs_[i].id) && plain(tabs_[i + 1].id)) {
      const RECT& rect = tabs_[i].rect;
      const int height = (rect.bottom - rect.top) / 2;
      const int top = rect.top + (rect.bottom - rect.top - height) / 2;
      Fill(dc, {rect.right - 1, top, rect.right, top + height},
           theme.separator);
    }
  }
  for (const TabGeometry& geometry : tabs_) {
    PaintTab(dc, geometry, state);
  }

  const bool new_tab_hovered = state.hover.part == Part::kNewTab;
  if (new_tab_hovered) {
    SetDCBrushColor(dc, theme.hover);
    SetDCPenColor(dc, theme.hover);
    HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
    Ellipse(dc, new_tab_.left, new_tab_.top, new_tab_.right, new_tab_.bottom);
    SelectObject(dc, brush);
    SelectObject(dc, pen);
  }
  {
    RECT area = new_tab_;
    HGDIOBJ previous = SelectObject(dc, glyph_font_);
    SetTextColor(dc, theme.text);
    DrawTextW(dc, kAddGlyph, -1, &area,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
  }

  if (caption_buttons_) {
    const auto background = [&](Part part) {
      if (state.hover.part != part && state.pressed.part != part) {
        return frame;
      }
      return part == Part::kClose ? theme.close_hover : theme.hover;
    };
    const COLORREF text = state.window_active ? theme.text : theme.text_dim;
    PaintButton(dc, minimize_, kMinimizeGlyph, glyph_font_,
                background(Part::kMinimize), text);
    PaintButton(dc, maximize_, state.maximized ? kRestoreGlyph : kMaximizeGlyph,
                glyph_font_, background(Part::kMaximize), text);
    const bool close_hot =
        state.hover.part == Part::kClose || state.pressed.part == Part::kClose;
    PaintButton(dc, close_, kCloseGlyph, glyph_font_, background(Part::kClose),
                close_hot ? RGB(255, 255, 255) : text);
  }
}

}  // namespace brouk::win
