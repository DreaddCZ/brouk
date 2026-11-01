#include "platform/win/bookmarks_bar.h"

#include <shlobj.h>
#include <windowsx.h>
#include <wrl/implements.h>

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <optional>
#include <string>
#include <utility>

#include "platform/win/controls.h"
#include "platform/win/favicon_cache.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukBookmarksBar";
constexpr int kHeight = 32;         // DIPs.
constexpr int kItemPadding = 8;     // Left and right in an item.
constexpr int kItemGap = 2;         // Between items.
constexpr int kMaxTextWidth = 150;  // A long name is cut (Chrome).
constexpr int kGlyphWidth = 16;
constexpr int kDragThreshold = 4;
// Glyphs of the "Segoe MDL2 Assets" icon font.
constexpr wchar_t kFolderGlyph[] = L"";
constexpr wchar_t kPageGlyph[] = L"";  // Globe.
constexpr wchar_t kOverflowText[] = L"»";

// Menu commands: the bookmarks of a menu from 1, the context menu's own
// above them.
enum MenuCommand : UINT {
  kOpenInNewTabCommand = 60000,
  kOpenInNewWindowCommand,
  kOpenAllCommand,
  kEditCommand,
  kDeleteCommand,
  kAddBookmarkCommand,
  kAddFolderCommand,
  kManagerCommand,
  kAlwaysShowCommand,
};

bool RegisterWindowClass() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

// Menu text shows "&" as a mnemonic unless doubled.
std::wstring MenuText(const std::wstring& text) {
  std::wstring escaped;
  for (wchar_t c : text) {
    escaped += c;
    if (c == L'&') {
      escaped += L'&';
    }
  }
  return escaped;
}

// A link dropped from outside: its address and, when the source gave one,
// its title.
struct DroppedLink {
  std::string url;
  std::string title;
};

// Text of `format` in `data` (UTF-16), or nothing.
std::optional<std::wstring> ReadText(IDataObject* data, CLIPFORMAT format) {
  FORMATETC wanted = {format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM medium = {};
  if (FAILED(data->GetData(&wanted, &medium))) {
    return std::nullopt;
  }
  std::optional<std::wstring> text;
  const SIZE_T bytes = GlobalSize(medium.hGlobal);
  if (const auto* chars =
          static_cast<const wchar_t*>(GlobalLock(medium.hGlobal))) {
    const std::size_t length = wcsnlen(chars, bytes / sizeof(wchar_t));
    text.emplace(chars, length);
    GlobalUnlock(medium.hGlobal);
  }
  ReleaseStgMedium(&medium);
  return text;
}

// The title Chromium gives a dragged link: the name of its shortcut file
// ("Example Domain.url").
std::string ReadLinkTitle(IDataObject* data) {
  const auto format =
      static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW));
  FORMATETC wanted = {format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM medium = {};
  if (FAILED(data->GetData(&wanted, &medium))) {
    return {};
  }
  std::string title;
  if (GlobalSize(medium.hGlobal) >= sizeof(FILEGROUPDESCRIPTORW)) {
    if (const auto* group = static_cast<const FILEGROUPDESCRIPTORW*>(
            GlobalLock(medium.hGlobal))) {
      if (group->cItems >= 1) {
        const wchar_t* name = group->fgd[0].cFileName;
        std::wstring file(name, wcsnlen(name, MAX_PATH));
        if (file.size() > 4 &&
            _wcsicmp(file.c_str() + file.size() - 4, L".url") == 0) {
          file.resize(file.size() - 4);
          title = WideToUtf8(file);
        }
      }
      GlobalUnlock(medium.hGlobal);
    }
  }
  ReleaseStgMedium(&medium);
  return title;
}

// Only web and file addresses become bookmarks (as on macOS); the data
// comes from another program, so it is checked and limited in size.
std::optional<DroppedLink> ReadDroppedLink(IDataObject* data) {
  constexpr std::size_t kMaxUrl = 2 * 1024 * 1024;  // Chromium's limit.
  const auto url_format =
      static_cast<CLIPFORMAT>(RegisterClipboardFormatW(CFSTR_INETURLW));
  std::optional<std::wstring> text = ReadText(data, url_format);
  if (!text) {
    text = ReadText(data, CF_UNICODETEXT);
  }
  if (!text) {
    return std::nullopt;
  }
  std::wstring url = *text;
  // The first line, without surrounding spaces.
  url = url.substr(0, url.find_first_of(L"\r\n"));
  const auto first = url.find_first_not_of(L" \t");
  const auto last = url.find_last_not_of(L" \t");
  if (first == std::wstring::npos || url.size() > kMaxUrl) {
    return std::nullopt;
  }
  url = url.substr(first, last - first + 1);
  auto starts = [&url](const wchar_t* prefix) {
    const std::size_t length = wcslen(prefix);
    return url.size() > length && _wcsnicmp(url.c_str(), prefix, length) == 0;
  };
  if (!starts(L"http://") && !starts(L"https://") && !starts(L"file://")) {
    return std::nullopt;
  }
  if (url.find_first_of(L" \t") != std::wstring::npos) {
    return std::nullopt;  // Text, not an address.
  }
  return DroppedLink{.url = WideToUtf8(url), .title = ReadLinkTitle(data)};
}

}  // namespace

// The bar as an OLE drop target for links dragged from pages or other
// apps (011). Holds the bar until it is detached (the bar's destructor).
class LinkDropTarget
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          IDropTarget> {
 public:
  explicit LinkDropTarget(BookmarksBar& bar) : bar_(&bar) {}
  void Detach() { bar_ = nullptr; }

  STDMETHODIMP DragEnter(IDataObject* data,
                         DWORD /*keys*/,
                         POINTL point,
                         DWORD* effect) override {
    link_ = data && ReadDroppedLink(data).has_value();
    return DragOver(0, point, effect);
  }
  STDMETHODIMP DragOver(DWORD /*keys*/, POINTL point, DWORD* effect) override {
    if (!link_ || !bar_) {
      *effect = DROPEFFECT_NONE;
      return S_OK;
    }
    bar_->OnLinkDragOver({point.x, point.y});
    // A link is bookmarked, not moved: Chromium offers copy and link.
    *effect &= DROPEFFECT_COPY | DROPEFFECT_LINK;
    if (*effect & DROPEFFECT_LINK) {
      *effect = DROPEFFECT_LINK;
    }
    return S_OK;
  }
  STDMETHODIMP DragLeave() override {
    if (bar_) {
      bar_->OnLinkDragLeave();
    }
    return S_OK;
  }
  STDMETHODIMP Drop(IDataObject* data,
                    DWORD /*keys*/,
                    POINTL point,
                    DWORD* effect) override {
    std::optional<DroppedLink> link =
        data ? ReadDroppedLink(data) : std::nullopt;
    if (!link || !bar_) {
      if (bar_) {
        bar_->OnLinkDragLeave();
      }
      *effect = DROPEFFECT_NONE;
      return S_OK;
    }
    bar_->OnLinkDrop({point.x, point.y}, link->url, link->title);
    *effect = (*effect & DROPEFFECT_LINK) ? DROPEFFECT_LINK : DROPEFFECT_COPY;
    return S_OK;
  }

 private:
  BookmarksBar* bar_;  // Not owned; null once the bar is gone.
  bool link_ = false;
};

BookmarksBar::BookmarksBar(HWND parent, Delegate& delegate)
    : delegate_(delegate) {
  if (!RegisterWindowClass()) {
    return;
  }
  window_ = CreateWindowExW(0, kWindowClass, L"Bookmarks bar",
                            WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent,
                            nullptr, GetModuleHandleW(nullptr), nullptr);
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(window_, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(&BookmarksBar::WindowProc));
    drop_target_ = Microsoft::WRL::Make<LinkDropTarget>(*this);
    // Fails without OLE on the thread (Chromium initialises it on the UI
    // thread); the bar then takes no drops.
    if (drop_target_ && FAILED(RegisterDragDrop(window_, drop_target_.Get()))) {
      drop_target_.Reset();
    }
  }
}

BookmarksBar::~BookmarksBar() {
  if (window_ && drop_target_) {
    RevokeDragDrop(window_);
    drop_target_->Detach();
  }
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
}

int BookmarksBar::Height(UINT dpi) {
  return MulDiv(kHeight, static_cast<int>(dpi), 96);
}

int BookmarksBar::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(window_)), 96);
}

void BookmarksBar::Update(const BookmarkModel& model,
                          const ProfileId& profile,
                          const Theme& theme,
                          HFONT text_font,
                          HFONT icon_font,
                          FaviconCache* favicons) {
  model_ = &model;
  favicons_ = favicons;
  profile_ = profile;
  theme_ = theme;
  text_font_ = text_font;
  icon_font_ = icon_font;
  items_.clear();
  if (const BookmarkItem* bar = model.Find(BookmarkModel::kBarId)) {
    for (BookmarkId child : bar->children) {
      const BookmarkItem* item = model.Find(child);
      if (!item || !model.IsVisibleIn(child, profile)) {
        continue;
      }
      const std::string& name =
          item->name.empty() && !item->folder ? item->url : item->name;
      items_.push_back({.id = child,
                        .folder = item->folder,
                        .name = Utf8ToWide(name),
                        .url = item->folder ? std::string() : item->url,
                        .bounds = {}});
    }
  }
  Layout();
  InvalidateRect(window_, nullptr, FALSE);
}

void BookmarksBar::Redraw() {
  InvalidateRect(window_, nullptr, FALSE);
}

void BookmarksBar::SetBounds(const RECT& bounds) {
  SetWindowPos(window_, nullptr, bounds.left, bounds.top,
               bounds.right - bounds.left, bounds.bottom - bounds.top,
               SWP_NOZORDER | SWP_NOACTIVATE);
  Layout();
}

void BookmarksBar::SetVisible(bool visible) {
  ShowWindow(window_, visible ? SW_SHOWNA : SW_HIDE);
}

void BookmarksBar::Layout() {
  RECT client = {};
  GetClientRect(window_, &client);
  HDC dc = GetDC(window_);
  HGDIOBJ previous = SelectObject(dc, text_font_);
  const int margin = Scale(6);
  const int top = Scale(3);
  const int bottom = client.bottom - Scale(3);
  const int overflow_width = Scale(28);
  int x = margin;
  first_hidden_ = items_.size();
  for (std::size_t i = 0; i < items_.size(); ++i) {
    Item& item = items_[i];
    RECT measured = {0, 0, Scale(kMaxTextWidth), 0};
    DrawTextW(dc, item.name.c_str(), -1, &measured,
              DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
    const int text =
        std::min(static_cast<int>(measured.right), Scale(kMaxTextWidth));
    const int width = Scale(kItemPadding) * 2 + Scale(kGlyphWidth) +
                      (item.name.empty() ? 0 : Scale(6) + text);
    // The last items give way to the "»" button when not all fit.
    const int limit =
        client.right - margin - (i + 1 < items_.size() ? overflow_width : 0);
    if (x + width > limit) {
      first_hidden_ = i;
      break;
    }
    item.bounds = {x, top, x + width, bottom};
    x += width + Scale(kItemGap);
  }
  for (std::size_t i = first_hidden_; i < items_.size(); ++i) {
    items_[i].bounds = {};
  }
  overflow_ = first_hidden_ < items_.size()
                  ? RECT{client.right - margin - overflow_width, top,
                         client.right - margin, bottom}
                  : RECT{};
  SelectObject(dc, previous);
  ReleaseDC(window_, dc);
}

void BookmarksBar::Paint() {
  PAINTSTRUCT paint = {};
  HDC dc = BeginPaint(window_, &paint);
  RECT client = {};
  GetClientRect(window_, &client);
  SetDCBrushColor(dc, theme_.toolbar);
  FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  SetBkMode(dc, TRANSPARENT);
  if (items_.empty()) {
    // As Chrome: a hint where bookmarks go.
    SelectObject(dc, text_font_);
    SetTextColor(dc, theme_.text_dim);
    RECT hint = {Scale(12), 0, client.right, client.bottom};
    DrawTextW(dc,
              L"For quick access, place your bookmarks here on the bookmarks "
              L"bar (Ctrl+D)",
              -1, &hint, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
  }
  auto draw_background = [&](const RECT& bounds, COLORREF color) {
    HGDIOBJ pen = SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, color);
    HGDIOBJ brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    RoundRect(dc, bounds.left, bounds.top, bounds.right + 1, bounds.bottom + 1,
              Scale(8), Scale(8));
    SelectObject(dc, brush);
    SelectObject(dc, pen);
  };
  for (std::size_t i = 0; i < first_hidden_; ++i) {
    const Item& item = items_[i];
    const bool into = drop_ && drop_->into == item.id;
    if (static_cast<int>(i) == hover_ || into) {
      draw_background(item.bounds, theme_.hover);
    }
    RECT glyph = {item.bounds.left + Scale(kItemPadding), item.bounds.top,
                  item.bounds.left + Scale(kItemPadding) + Scale(kGlyphWidth),
                  item.bounds.bottom};
    // The site's icon from the history, else a globe (011).
    const int icon_size = Scale(16);
    const Favicon* icon =
        favicons_ && !item.folder
            ? favicons_->IconFor(profile_, item.url, icon_size)
            : nullptr;
    if (icon) {
      icon->Draw(dc, glyph.left + (glyph.right - glyph.left - icon_size) / 2,
                 glyph.top + (glyph.bottom - glyph.top - icon_size) / 2);
    } else {
      SelectObject(dc, icon_font_);
      SetTextColor(dc, theme_.text_dim);
      DrawTextW(dc, item.folder ? kFolderGlyph : kPageGlyph, -1, &glyph,
                DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    }
    RECT text = {glyph.right + Scale(6), item.bounds.top,
                 item.bounds.right - Scale(kItemPadding), item.bounds.bottom};
    SelectObject(dc, text_font_);
    SetTextColor(dc, theme_.text);
    DrawTextW(dc, item.name.c_str(), -1, &text,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
  }
  if (overflow_.right > overflow_.left) {
    if (hover_ == kOverflow) {
      draw_background(overflow_, theme_.hover);
    }
    SelectObject(dc, text_font_);
    SetTextColor(dc, theme_.text);
    RECT area = overflow_;
    DrawTextW(dc, kOverflowText, -1, &area,
              DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
  }
  // Where a dragged item would go: a line between items.
  if ((dragging_ || link_drag_) && drop_ && drop_->into == 0) {
    int x = Scale(6);
    for (std::size_t i = 0; i < first_hidden_; ++i) {
      const BookmarkItem* bar = model_->Find(BookmarkModel::kBarId);
      const auto it = std::ranges::find(bar->children, items_[i].id);
      const auto index = static_cast<std::size_t>(it - bar->children.begin());
      if (index >= drop_->index) {
        x = items_[i].bounds.left - Scale(1);
        break;
      }
      x = items_[i].bounds.right + Scale(1);
    }
    RECT line = {x - Scale(1), Scale(4), x + Scale(1),
                 client.bottom - Scale(4)};
    SetDCBrushColor(dc, GetSysColor(COLOR_HIGHLIGHT));
    FillRect(dc, &line, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
  }
  EndPaint(window_, &paint);
}

int BookmarksBar::HitTest(POINT point) const {
  for (std::size_t i = 0; i < first_hidden_; ++i) {
    if (PtInRect(&items_[i].bounds, point)) {
      return static_cast<int>(i);
    }
  }
  if (PtInRect(&overflow_, point)) {
    return kOverflow;
  }
  return -1;
}

void BookmarksBar::OnLinkDragOver(POINT screen) {
  if (!model_) {
    return;
  }
  ScreenToClient(window_, &screen);
  link_drag_ = true;
  drop_ = DropAt(screen);
  InvalidateRect(window_, nullptr, FALSE);
}

void BookmarksBar::OnLinkDragLeave() {
  link_drag_ = false;
  drop_.reset();
  InvalidateRect(window_, nullptr, FALSE);
}

void BookmarksBar::OnLinkDrop(POINT screen,
                              const std::string& url,
                              const std::string& title) {
  if (!model_) {
    return;
  }
  ScreenToClient(window_, &screen);
  const DropTarget target = DropAt(screen);
  OnLinkDragLeave();
  if (target.into != 0) {
    delegate_.DropLink(url, title, target.into, BookmarkModel::kEnd);
  } else {
    delegate_.DropLink(url, title, BookmarkModel::kBarId, target.index);
  }
}

BookmarksBar::DropTarget BookmarksBar::DropAt(POINT point) const {
  const BookmarkItem* bar = model_->Find(BookmarkModel::kBarId);
  DropTarget target{.index = bar ? bar->children.size() : 0, .into = 0};
  if (!bar) {
    return target;
  }
  for (std::size_t i = 0; i < first_hidden_; ++i) {
    const Item& item = items_[i];
    const int width = item.bounds.right - item.bounds.left;
    const auto it = std::ranges::find(bar->children, item.id);
    const auto index = static_cast<std::size_t>(it - bar->children.begin());
    // The middle of a folder drops into it; elsewhere between items.
    if (item.folder && (!pressed_ || items_[*pressed_].id != item.id) &&
        point.x >= item.bounds.left + width / 4 &&
        point.x < item.bounds.right - width / 4) {
      return {.index = 0, .into = item.id};
    }
    if (point.x < item.bounds.left + width / 2) {
      return {.index = index, .into = 0};
    }
  }
  return target;
}

void BookmarksBar::OnClick(int hit, bool control, bool shift, bool middle) {
  if (hit == kOverflow) {
    ShowOverflowMenu();
    return;
  }
  if (hit < 0) {
    return;
  }
  const Item item = items_[static_cast<std::size_t>(hit)];
  if (item.folder) {
    ShowFolderMenu(item.id, item.bounds);
    return;
  }
  // Click: this tab; Ctrl or middle: a background tab; Ctrl+Shift: a new
  // tab in front (011, as Chrome).
  const BookmarkOpen how = control && shift    ? BookmarkOpen::kForegroundTab
                           : control || middle ? BookmarkOpen::kBackgroundTab
                                               : BookmarkOpen::kCurrentTab;
  delegate_.OpenBookmark(item.id, how);
}

void BookmarksBar::AppendFolder(HMENU menu,
                                BookmarkId folder,
                                std::vector<BookmarkId>& ids) {
  const BookmarkItem* parent = model_->Find(folder);
  if (!parent) {
    return;
  }
  bool any = false;
  for (BookmarkId child : parent->children) {
    const BookmarkItem* item = model_->Find(child);
    if (!item || !model_->IsVisibleIn(child, profile_)) {
      continue;
    }
    any = true;
    const std::wstring name =
        MenuText(Utf8ToWide(item->name.empty() ? item->url : item->name));
    if (item->folder) {
      HMENU submenu = CreatePopupMenu();
      AppendFolder(submenu, child, ids);
      AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu),
                  name.c_str());
      continue;
    }
    ids.push_back(child);
    AppendMenuW(menu, MF_STRING, ids.size(), name.c_str());
    SetMenuIcon(menu, static_cast<UINT>(ids.size()), item->url);
  }
  if (!any) {
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"(empty)");
  }
}

void BookmarksBar::RunMenu(HMENU menu,
                           const RECT& anchor,
                           std::vector<BookmarkId>& ids) {
  RECT screen = anchor;
  MapWindowPoints(window_, nullptr, reinterpret_cast<POINT*>(&screen), 2);
  const UINT command = static_cast<UINT>(TrackPopupMenuEx(
      menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
      screen.left, screen.bottom, window_, nullptr));
  DestroyMenu(menu);
  menu_icons_.clear();
  if (command >= 1 && command <= ids.size()) {
    delegate_.OpenBookmark(ids[command - 1], KeyDown(VK_CONTROL)
                                                 ? BookmarkOpen::kBackgroundTab
                                                 : BookmarkOpen::kCurrentTab);
  }
}

void BookmarksBar::SetMenuIcon(HMENU menu,
                               UINT command,
                               const std::string& url) {
  if (!favicons_) {
    return;
  }
  Favicon icon = favicons_->CopyOf(
      profile_, url,
      GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(window_)));
  if (icon.empty()) {
    return;
  }
  MENUITEMINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = MIIM_BITMAP;
  info.hbmpItem = icon.bitmap();
  SetMenuItemInfoW(menu, command, FALSE, &info);
  menu_icons_.push_back(std::move(icon));
}

void BookmarksBar::ShowFolderMenu(BookmarkId folder, const RECT& anchor) {
  std::vector<BookmarkId> ids;
  HMENU menu = CreatePopupMenu();
  AppendFolder(menu, folder, ids);
  RunMenu(menu, anchor, ids);
}

void BookmarksBar::ShowOverflowMenu() {
  std::vector<BookmarkId> ids;
  HMENU menu = CreatePopupMenu();
  for (std::size_t i = first_hidden_; i < items_.size(); ++i) {
    const Item& item = items_[i];
    if (item.folder) {
      HMENU submenu = CreatePopupMenu();
      AppendFolder(submenu, item.id, ids);
      AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu),
                  MenuText(item.name).c_str());
      continue;
    }
    ids.push_back(item.id);
    AppendMenuW(menu, MF_STRING, ids.size(), MenuText(item.name).c_str());
    SetMenuIcon(menu, static_cast<UINT>(ids.size()), item.url);
  }
  RunMenu(menu, overflow_, ids);
}

void BookmarksBar::ShowContextMenu(int hit, POINT screen) {
  const BookmarkItem* bar = model_->Find(BookmarkModel::kBarId);
  if (!bar) {
    return;
  }
  const Item* item =
      hit >= 0 ? &items_[static_cast<std::size_t>(hit)] : nullptr;
  // New items go after the clicked one, or at the end.
  std::size_t index = bar->children.size();
  if (item) {
    const auto it = std::ranges::find(bar->children, item->id);
    index = static_cast<std::size_t>(it - bar->children.begin()) + 1;
  }
  HMENU menu = CreatePopupMenu();
  int bookmarks_inside = 0;
  if (item && item->folder) {
    if (const BookmarkItem* folder = model_->Find(item->id)) {
      for (BookmarkId child : folder->children) {
        const BookmarkItem* inner = model_->Find(child);
        if (inner && !inner->folder && model_->IsVisibleIn(child, profile_)) {
          ++bookmarks_inside;
        }
      }
    }
    const std::wstring open_all =
        L"Open all (" + std::to_wstring(bookmarks_inside) + L")";
    AppendMenuW(menu, MF_STRING | (bookmarks_inside > 0 ? 0 : MF_GRAYED),
                kOpenAllCommand, open_all.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  } else if (item) {
    AppendMenuW(menu, MF_STRING, kOpenInNewTabCommand, L"Open in new tab");
    AppendMenuW(menu, MF_STRING, kOpenInNewWindowCommand,
                L"Open in new window");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  }
  if (item) {
    AppendMenuW(menu, MF_STRING, kEditCommand, L"Edit…");
    AppendMenuW(menu, MF_STRING, kDeleteCommand, L"Delete");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  }
  AppendMenuW(menu, MF_STRING, kAddBookmarkCommand, L"Add bookmark…");
  AppendMenuW(menu, MF_STRING, kAddFolderCommand, L"Add folder…");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kManagerCommand,
              L"Bookmark manager\tCtrl+Shift+O");
  AppendMenuW(
      menu, MF_STRING | (delegate_.BookmarksBarAlwaysShown() ? MF_CHECKED : 0),
      kAlwaysShowCommand, L"Always show bookmarks bar\tCtrl+Shift+B");
  const UINT command = static_cast<UINT>(TrackPopupMenuEx(
      menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
      screen.x, screen.y, window_, nullptr));
  DestroyMenu(menu);
  const BookmarkId id = item ? item->id : 0;
  switch (command) {
    case kOpenInNewTabCommand:
      delegate_.OpenBookmark(id, BookmarkOpen::kForegroundTab);
      break;
    case kOpenInNewWindowCommand:
      delegate_.OpenBookmark(id, BookmarkOpen::kNewWindow);
      break;
    case kOpenAllCommand:
      delegate_.OpenAllBookmarks(id);
      break;
    case kEditCommand:
      delegate_.EditBookmark(id);
      break;
    case kDeleteCommand:
      delegate_.RemoveBookmark(id);
      break;
    case kAddBookmarkCommand:
      delegate_.AddBookmark(BookmarkModel::kBarId, index);
      break;
    case kAddFolderCommand:
      delegate_.AddBookmarkFolder(BookmarkModel::kBarId, index);
      break;
    case kManagerCommand:
      delegate_.ShowBookmarkManager();
      break;
    case kAlwaysShowCommand:
      delegate_.ToggleBookmarksBar();
      break;
    default:
      break;
  }
}

LRESULT CALLBACK BookmarksBar::WindowProc(HWND window,
                                          UINT message,
                                          WPARAM wparam,
                                          LPARAM lparam) {
  auto* self =
      reinterpret_cast<BookmarksBar*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

LRESULT BookmarksBar::HandleMessage(UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam) {
  const POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
  switch (message) {
    case WM_ERASEBKGND:
      return 1;  // Painted in WM_PAINT.
    case WM_PAINT:
      Paint();
      return 0;
    case WM_SIZE:
      Layout();
      InvalidateRect(window_, nullptr, FALSE);
      return 0;
    case WM_MOUSEMOVE: {
      if (!tracking_mouse_) {
        TRACKMOUSEEVENT track = {};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = window_;
        tracking_mouse_ = TrackMouseEvent(&track) != FALSE;
      }
      if (pressed_ && !dragging_ &&
          (std::abs(point.x - press_point_.x) > Scale(kDragThreshold) ||
           std::abs(point.y - press_point_.y) > Scale(kDragThreshold))) {
        dragging_ = true;
      }
      if (dragging_) {
        drop_ = DropAt(point);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
      }
      if (const int hit = HitTest(point); hit != hover_) {
        hover_ = hit;
        InvalidateRect(window_, nullptr, FALSE);
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      tracking_mouse_ = false;
      if (hover_ != -1) {
        hover_ = -1;
        InvalidateRect(window_, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONDOWN:
      if (const int hit = HitTest(point); hit >= 0) {
        pressed_ = static_cast<std::size_t>(hit);
        press_point_ = point;
        SetCapture(window_);
      }
      return 0;
    case WM_LBUTTONUP: {
      const std::optional<std::size_t> pressed = pressed_;
      const bool dragged = dragging_;
      const std::optional<DropTarget> drop = drop_;
      pressed_.reset();
      dragging_ = false;
      drop_.reset();
      if (GetCapture() == window_) {
        ReleaseCapture();
      }
      InvalidateRect(window_, nullptr, FALSE);
      if (dragged && pressed && drop) {
        const BookmarkId id = items_[*pressed].id;
        if (drop->into != 0) {
          delegate_.MoveBookmark(id, drop->into, BookmarkModel::kEnd);
        } else {
          delegate_.MoveBookmark(id, BookmarkModel::kBarId, drop->index);
        }
        return 0;
      }
      const int hit = HitTest(point);
      if (!dragged && (hit == kOverflow ||
                       (pressed && hit == static_cast<int>(*pressed)))) {
        OnClick(hit, KeyDown(VK_CONTROL), KeyDown(VK_SHIFT), false);
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
      pressed_.reset();
      dragging_ = false;
      drop_.reset();
      return 0;
    case WM_MBUTTONUP:
      if (const int hit = HitTest(point); hit >= 0) {
        OnClick(hit, false, false, true);
      }
      return 0;
    case WM_RBUTTONUP: {
      POINT screen = point;
      ClientToScreen(window_, &screen);
      const int hit = HitTest(point);
      ShowContextMenu(hit == kOverflow ? -1 : hit, screen);
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
