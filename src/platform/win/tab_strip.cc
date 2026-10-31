#include "platform/win/tab_strip.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

#include "core/profile_colors.h"
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
// A profile group: [label] gap [tabs] [+] group gap.
constexpr int kLabelGap = 4;
constexpr int kGroupGap = 8;
constexpr int kMaxLabelWidth = 160;
constexpr int kLabelHeight = 20;
constexpr int kGroupLineHeight = 2;
// Empty title bar always left for dragging the window.
constexpr int kMinDragWidth = 48;
constexpr int kTabMaxWidth = 240;
constexpr int kTabMinWidth = 44;
constexpr int kTabPadding = 10;
constexpr int kIconSize = 16;
constexpr int kCloseButtonSize = 20;
constexpr int kTabCornerRadius = 8;

// Glyphs of the "Segoe MDL2 Assets" icon font (Windows 10 and 11).
constexpr wchar_t kMinimizeGlyph[] = L"\uE921";
constexpr wchar_t kMaximizeGlyph[] = L"\uE922";
constexpr wchar_t kRestoreGlyph[] = L"\uE923";
constexpr wchar_t kCloseGlyph[] = L"\uE8BB";
constexpr wchar_t kAddGlyph[] = L"\uE710";
constexpr wchar_t kPageGlyph[] = L"\uE774";  // Globe: a page without icon.

COLORREF FromRgb(std::uint32_t rgb) {
  return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

COLORREF ProfileFill(const std::string& color, bool dark) {
  const ProfileColorShades shades = ShadesOfProfileColor(color);
  return FromRgb(dark ? shades.dark : shades.light);
}

// Text on the profile colour: black on light fills (and every dark-mode
// shade), else white; as on macOS.
COLORREF ProfileText(const std::string& color, bool dark) {
  return !dark && ShadesOfProfileColor(color).light_text_on_fill
             ? RGB(255, 255, 255)
             : RGB(0, 0, 0);
}

COLORREF Blend(COLORREF over, COLORREF under, double alpha) {
  auto mix = [alpha](int a, int b) {
    return static_cast<int>(std::lround(a * alpha + b * (1 - alpha)));
  };
  return RGB(mix(GetRValue(over), GetRValue(under)),
             mix(GetGValue(over), GetGValue(under)),
             mix(GetBValue(over), GetBValue(under)));
}

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
  for (HFONT font :
       {title_font_, label_font_, glyph_font_, small_glyph_font_}) {
    if (font) {
      DeleteObject(font);
    }
  }
}

int TabStrip::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(dpi_), 96);
}

void TabStrip::UpdateFonts() {
  for (HFONT* font :
       {&title_font_, &label_font_, &glyph_font_, &small_glyph_font_}) {
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
  LOGFONTW label = metrics.lfMessageFont;
  label.lfWeight = FW_SEMIBOLD;
  label.lfHeight = -Scale(11);
  label_font_ = CreateFontIndirectW(&label);
  glyph_font_ = GlyphFont(dpi_, 10);
  small_glyph_font_ = GlyphFont(dpi_, 8);
}

void TabStrip::Layout(const RECT& bounds,
                      UINT dpi,
                      const TabStripModel& model,
                      bool caption_buttons,
                      const LookOf& look) {
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
  const int reserved = Scale(kMinDragWidth);

  // One layout group per profile group, with its label and "+" as fixed
  // width.
  const std::vector<TabStripModel::Group> groups = model.groups();
  groups_.clear();
  HDC dc = GetDC(nullptr);
  HGDIOBJ previous_font = SelectObject(dc, label_font_);
  for (const TabStripModel::Group& group : groups) {
    GroupGeometry geometry;
    geometry.profile = group.profile;
    const GroupLook group_look = look(group.profile);
    geometry.name = Utf8ToWide(group_look.name);
    geometry.color = group_look.color;
    geometry.count = group.count;
    geometry.collapsed = group.collapsed;
    const std::wstring text =
        group.count > 0 ? geometry.name + L"  " + std::to_wstring(group.count)
                        : geometry.name;
    SIZE size = {};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()),
                          &size);
    const int width =
        std::min(static_cast<int>(size.cx) + Scale(16), Scale(kMaxLabelWidth));
    geometry.label = {0, 0, width, 0};
    groups_.push_back(geometry);
  }
  SelectObject(dc, previous_font);
  ReleaseDC(nullptr, dc);

  TabStripLayoutInput input;
  input.width = std::max(0, right - left - reserved);
  input.metrics.max_tab_width = Scale(kTabMaxWidth);
  input.metrics.min_tab_width = Scale(kTabMinWidth);
  input.metrics.pinned_min_width = Scale(96);
  input.metrics.tab_gap = 0;
  input.metrics.more_button_width = Scale(40);
  const std::optional<std::size_t> active = model.active_index();
  for (std::size_t g = 0; g < groups.size(); ++g) {
    const TabStripModel::Group& group = groups[g];
    input.groups.push_back({.fixed_width = static_cast<double>(
                                groups_[g].label.right + Scale(kLabelGap) +
                                Scale(kNewTabButtonSize) + Scale(kGroupGap)),
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
  const int label_height = Scale(kLabelHeight);
  const int label_top = tab_top + (bounds.bottom - tab_top - label_height) / 2;
  const int button = Scale(kNewTabButtonSize);
  const int button_top = tab_top + (bounds.bottom - tab_top - button) / 2;
  for (std::size_t g = 0; g < groups.size() && g < layout.size(); ++g) {
    const TabStripGroupLayout& group_layout = layout[g];
    GroupGeometry& group = groups_[g];
    group.auto_collapsed = group_layout.auto_collapsed;
    const int label_left = static_cast<int>(std::lround(x));
    group.label = {label_left, label_top, label_left + group.label.right,
                   label_top + label_height};
    x = group.label.right + Scale(kLabelGap);
    const int tabs_left = static_cast<int>(std::lround(x));
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
    const int tabs_right = static_cast<int>(std::lround(x));
    group.line = tabs_right > tabs_left
                     ? RECT{tabs_left, bounds.bottom - Scale(kGroupLineHeight),
                            tabs_right, bounds.bottom}
                     : RECT{};
    group.plus = {tabs_right, button_top, tabs_right + button,
                  button_top + button};
    x = tabs_right + button + Scale(kGroupGap);
  }
}

bool TabStrip::IsAutoCollapsed(const ProfileId& profile) const {
  for (const GroupGeometry& group : groups_) {
    if (group.profile == profile) {
      return group.auto_collapsed;
    }
  }
  return false;
}

std::wstring TabStrip::ToolTip(const Hit& hit) const {
  for (const GroupGeometry& group : groups_) {
    if (group.profile != hit.profile) {
      continue;
    }
    if (hit.part == Part::kNewTab) {
      return L"New tab in " + group.name;
    }
    if (hit.part == Part::kGroupLabel) {
      const std::wstring action = group.auto_collapsed ? L"show its tabs"
                                  : group.collapsed    ? L"expand"
                                                       : L"collapse";
      return group.name + L": " + std::to_wstring(group.count) +
             (group.count == 1 ? L" tab" : L" tabs") + L". Click to " + action +
             L".";
    }
  }
  return {};
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
  for (const GroupGeometry& group : groups_) {
    if (Contains(group.label, point)) {
      return {Part::kGroupLabel, 0, group.profile};
    }
    if (Contains(group.plus, point)) {
      return {Part::kNewTab, 0, group.profile};
    }
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

std::optional<ProfileId> TabStrip::GroupAt(POINT point) const {
  if (!Contains(bounds_, point)) {
    return std::nullopt;
  }
  // Groups are laid out left to right, each from its label to its "+".
  for (const GroupGeometry& group : groups_) {
    if (point.x >= group.label.left && point.x < group.plus.right) {
      return group.profile;
    }
  }
  return std::nullopt;
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
    const std::wstring text =
        tab ? Utf8ToWide(tab->DisplayTitle()) : std::wstring(L"New Tab");
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

void TabStrip::PaintGroup(HDC dc,
                          const GroupGeometry& group,
                          const PaintState& state) const {
  const Theme& theme = *state.theme;
  const COLORREF color = ProfileFill(group.color, state.dark);
  const COLORREF frame =
      state.window_active ? theme.frame : theme.frame_inactive;
  // The label: a chip in the profile's colour, outlined when collapsed so
  // it reads as "closed" at a glance (as on macOS).
  const bool collapsed = group.collapsed || group.auto_collapsed;
  SetDCPenColor(dc, color);
  SetDCBrushColor(dc, collapsed ? Blend(color, frame, 0.15) : color);
  HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
  HGDIOBJ pen = SelectObject(dc, GetStockObject(DC_PEN));
  const int radius = Scale(12);
  RoundRect(dc, group.label.left, group.label.top, group.label.right,
            group.label.bottom, radius, radius);
  const std::wstring text =
      group.count > 0 ? group.name + L"  " + std::to_wstring(group.count)
                      : group.name;
  RECT text_area = group.label;
  InflateRect(&text_area, -Scale(6), 0);
  HGDIOBJ previous = SelectObject(dc, label_font_);
  SetTextColor(dc,
               collapsed ? theme.text : ProfileText(group.color, state.dark));
  DrawTextW(
      dc, text.c_str(), static_cast<int>(text.size()), &text_area,
      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  SelectObject(dc, previous);
  if (group.line.right > group.line.left) {
    Fill(dc, group.line, color);
  }
  // The group's "+", in its colour.
  const bool hovered =
      state.hover.part == Part::kNewTab && state.hover.profile == group.profile;
  if (hovered) {
    SetDCBrushColor(dc, theme.hover);
    SetDCPenColor(dc, theme.hover);
    Ellipse(dc, group.plus.left, group.plus.top, group.plus.right,
            group.plus.bottom);
  }
  SelectObject(dc, brush);
  SelectObject(dc, pen);
  RECT plus = group.plus;
  previous = SelectObject(dc, glyph_font_);
  SetTextColor(dc, color);
  DrawTextW(dc, kAddGlyph, -1, &plus,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, previous);
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

  for (const GroupGeometry& group : groups_) {
    PaintGroup(dc, group, state);
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
