#ifndef BROUK_PLATFORM_WIN_BOOKMARKS_BAR_H_
#define BROUK_PLATFORM_WIN_BOOKMARKS_BAR_H_

#include <oleidl.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/bookmark_model.h"
#include "core/profile_id.h"
#include "platform/win/favicon.h"
#include "platform/win/theme.h"

namespace brouk::win {

class FaviconCache;
class LinkDropTarget;

// Where a bookmark opens (011): click, Ctrl-click or middle-click,
// Ctrl+Shift-click, "Open in new window".
enum class BookmarkOpen {
  kCurrentTab,
  kBackgroundTab,
  kForegroundTab,
  kNewWindow,
};

// The bookmarks bar below the toolbar (011), as on macOS: the "Bookmarks
// bar" folder's items visible in the active tab's profile, folders as
// drop-down menus, a "»" menu for what does not fit, dragging to reorder or
// to move into a folder, dropping links from pages (OLE drag and drop), and
// the right-click menu. A child window of the
// browser window, drawn by Brouk. UI thread.
class BookmarksBar {
 public:
  // What the bar asks of its window.
  class Delegate {
   public:
    virtual void OpenBookmark(BookmarkId id, BookmarkOpen how) = 0;
    // Open all (n): the folder's bookmarks as new tabs.
    virtual void OpenAllBookmarks(BookmarkId folder) = 0;
    virtual void EditBookmark(BookmarkId id) = 0;
    virtual void RemoveBookmark(BookmarkId id) = 0;
    // Add bookmark… / Add folder… at `index` of `folder`.
    virtual void AddBookmark(BookmarkId folder, std::size_t index) = 0;
    virtual void AddBookmarkFolder(BookmarkId folder, std::size_t index) = 0;
    // An item dragged to `index` of `parent` (BookmarkModel::Move).
    virtual void MoveBookmark(BookmarkId id,
                              BookmarkId parent,
                              std::size_t index) = 0;
    // A link dropped from a page (or another app) at `index` of `parent`:
    // a new bookmark there (011).
    virtual void DropLink(const std::string& url,
                          const std::string& title,
                          BookmarkId parent,
                          std::size_t index) = 0;
    virtual void ShowBookmarkManager() = 0;
    // "Always show bookmarks bar" (Ctrl+Shift+B).
    virtual void ToggleBookmarksBar() = 0;
    virtual bool BookmarksBarAlwaysShown() = 0;

   protected:
    virtual ~Delegate() = default;
  };

  BookmarksBar(HWND parent, Delegate& delegate);
  ~BookmarksBar();
  BookmarksBar(const BookmarksBar&) = delete;
  BookmarksBar& operator=(const BookmarksBar&) = delete;

  // Shows the bar folder of `model` as seen in `profile`, with the
  // window's theme and fonts and the app's site icons (which outlive the
  // bar; `favicons` may be null).
  void Update(const BookmarkModel& model,
              const ProfileId& profile,
              const Theme& theme,
              HFONT text_font,
              HFONT icon_font,
              FaviconCache* favicons);
  // Site icons arrived: redraw.
  void Redraw();
  void SetBounds(const RECT& bounds);
  void SetVisible(bool visible);
  // The bar's height in pixels at `dpi`.
  static int Height(UINT dpi);
  HWND hwnd() const { return window_; }

 private:
  friend class LinkDropTarget;
  struct Item {
    BookmarkId id = 0;
    bool folder = false;
    std::wstring name;
    std::string url;  // Bookmarks only, for the site icon.
    RECT bounds = {};
  };

  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void Layout();
  void Paint();
  int Scale(int dips) const;
  // The item at `point` (client pixels), or the "»" button (-2), or none
  // (-1).
  int HitTest(POINT point) const;
  static constexpr int kOverflow = -2;
  // Where a dragged item would go: the index among the bar's children,
  // or into the folder `into`.
  struct DropTarget {
    std::size_t index = 0;
    BookmarkId into = 0;
  };
  DropTarget DropAt(POINT point) const;
  void OnClick(int hit, bool control, bool shift, bool middle);
  // A folder's (or the overflow's) contents as a menu under `anchor`.
  void ShowFolderMenu(BookmarkId folder, const RECT& anchor);
  void ShowOverflowMenu();
  // Appends `folder`'s visible items, folders as submenus; `ids` maps the
  // commands.
  void AppendFolder(HMENU menu,
                    BookmarkId folder,
                    std::vector<BookmarkId>& ids);
  void RunMenu(HMENU menu, const RECT& anchor, std::vector<BookmarkId>& ids);
  // Puts the site icon of `url` on the menu item with `command`.
  void SetMenuIcon(HMENU menu, UINT command, const std::string& url);
  void ShowContextMenu(int hit, POINT screen);
  // Links dragged over the bar from outside (LinkDropTarget); screen
  // pixels.
  void OnLinkDragOver(POINT screen);
  void OnLinkDragLeave();
  void OnLinkDrop(POINT screen,
                  const std::string& url,
                  const std::string& title);

  HWND window_ = nullptr;
  Delegate& delegate_;
  const BookmarkModel* model_ = nullptr;
  ProfileId profile_;
  Theme theme_;
  HFONT text_font_ = nullptr;
  HFONT icon_font_ = nullptr;
  FaviconCache* favicons_ = nullptr;
  // The site icons of the menu that shows (menus need them until closed).
  std::vector<Favicon> menu_icons_;
  std::vector<Item> items_;
  // Items from this index on do not fit and are in the "»" menu.
  std::size_t first_hidden_ = 0;
  RECT overflow_ = {};
  int hover_ = -1;
  bool tracking_mouse_ = false;
  // Dragging an item to another place (011).
  std::optional<std::size_t> pressed_;
  POINT press_point_ = {};
  bool dragging_ = false;
  std::optional<DropTarget> drop_;
  // A link from outside is over the bar.
  bool link_drag_ = false;
  Microsoft::WRL::ComPtr<LinkDropTarget> drop_target_;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_BOOKMARKS_BAR_H_
