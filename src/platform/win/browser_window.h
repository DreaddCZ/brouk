#ifndef BROUK_PLATFORM_WIN_BROWSER_WINDOW_H_
#define BROUK_PLATFORM_WIN_BROWSER_WINDOW_H_

#include <windows.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cef/search_suggest_request.h"
#include "cef/tab_client.h"
#include "core/address_suggestions.h"
#include "core/closed_tabs.h"
#include "core/profile_registry.h"
#include "core/session.h"
#include "core/tab_strip_model.h"
#include "include/cef_request_context.h"
#include "platform/win/suggestions_popup.h"
#include "platform/win/tab.h"
#include "platform/win/tab_panel.h"
#include "platform/win/tab_strip.h"
#include "platform/win/theme.h"

namespace brouk::win {

class BrowserWindow;

// The app, as the browser windows see it. UI thread.
class BrowserWindowOwner {
 public:
  // The request context of `profile`'s web data (005).
  virtual CefRefPtr<CefRequestContext> RequestContext(
      const ProfileId& profile) = 0;
  virtual ProfileId DefaultProfile() = 0;
  // A profile's name and colour for its group in the tab strip.
  virtual TabStrip::GroupLook ProfileLook(const ProfileId& profile) = 0;
  virtual TabClientDelegate::SearchEngine SearchEngine() = 0;
  // Opens a new browser window with a tab for `url` (empty: a new, empty
  // tab) in `profile` (empty: the default profile).
  virtual void OpenWindow(const std::string& url, const ProfileId& profile) = 0;
  // Opens an empty popup window (window.open() with a size, as Chrome) for
  // content of `width` x `height` pixels; null if it failed.
  virtual BrowserWindow* OpenPopupWindow(int width, int height) = 0;
  // The request context of internal pages (in memory only); only it can
  // load brouk:// pages.
  virtual CefRefPtr<CefRequestContext> InternalContext() = 0;
  // The bridge handler of an internal page shown in tab `tab` of `window`
  // (the licences, 023; history, 013), or null for pages that do not open
  // in a tab (yet).
  virtual std::unique_ptr<CefMessageRouterBrowserSide::Handler>
  InternalPageHandler(const std::string& page,
                      BrowserWindow& window,
                      TabId tab) = 0;
  // History (013): null before CEF is ready. A profile's history changed;
  // its most recent pages for the menu (null until read, which starts
  // reading them).
  virtual HistoryService* History() = 0;
  virtual void OnHistoryChanged(const ProfileId& profile) = 0;
  virtual const std::vector<HistoryMatch>* RecentPages(
      const ProfileId& profile) = 0;
  // History → Clear browsing data (013): the dialog on the history page.
  virtual void OpenClearBrowsingData() = 0;
  // Address bar suggestions (003): the search engine's suggestion URL
  // template (empty: none), and whether history pages are suggested.
  virtual std::string SearchSuggestTemplate() = 0;
  virtual bool HistorySuggestionsEnabled() = 0;
  // Downloads (014): the app's list (null before CEF is ready), the
  // setting downloads.ask_where_to_save, and unique paths in a profile's
  // download folder.
  virtual DownloadService* Downloads() = 0;
  virtual bool AskWhereToSaveDownloads() = 0;
  virtual DownloadTarget ReserveDownloadPath(const std::string& suggested,
                                             const ProfileId& profile) = 0;
  virtual void ReleaseDownloadPath(const std::string& path) = 0;
  virtual const std::vector<ProfileInfo>& ProfileList() = 0;
  // Opens the profile manager (brouk://profiles, 005) in its own window.
  virtual void OpenProfileManager() = 0;
  // The settings window (004) with `url` (brouk://settings/#...), or as it
  // is if `url` is empty.
  virtual void OpenSettings(const std::string& url) = 0;
  // Opens the help window (016), at a section if `anchor` is not empty.
  virtual void OpenHelp(const std::string& anchor) = 0;
  // Closes every window (the menu's Exit).
  virtual void CloseAllWindows() = 0;
  // Whether `window` is still open (popups opened from another window).
  virtual bool IsOpenWindow(const BrowserWindow* window) const = 0;
  // The window is gone; the owner deletes it (later, not from inside its
  // window procedure).
  virtual void OnWindowDestroyed(BrowserWindow* window) = 0;
  // Session restore (015): what a session holds changed (tabs, pages,
  // order, pinning, collapsing, the frame), and a normal window is about to
  // close its tabs (while its session state is still complete).
  virtual void OnSessionChanged() = 0;
  // Also asks first when closing would quit Brouk with downloads running
  // (014); false: the window stays open.
  virtual bool OnWindowClosing(BrowserWindow& window) = 0;
  // Reopen closed tabs (013): a tab closed on its own, a window closed as a
  // whole with its tabs, the list for the menu, and reopening one (nullopt:
  // the newest, Ctrl+Shift+T).
  virtual void OnTabClosedToReopen(ClosedTab tab) = 0;
  virtual void OnWindowClosedToReopen(std::int64_t window_id,
                                      std::vector<ClosedTab> tabs) = 0;
  virtual const std::deque<ClosedEntry>& RecentlyClosed() = 0;
  virtual void ReopenClosed(std::optional<std::int64_t> entry_id) = 0;

 protected:
  virtual ~BrowserWindowOwner() = default;
};

// A top-level browser window. Normal windows draw their own title bar with
// the tab strip in it (024, like Chrome and Edge), keeping the system's
// resizing, snapping and snap layouts; below it are the toolbar (back,
// forward, reload, address bar) and the active tab. Popup windows have the
// system title bar and a read-only address bar (Chrome). The tabs' order
// and selection follow the core's TabStripModel.
class BrowserWindow final : public TabHost {
 public:
  enum class Kind { kNormal, kPopup };

  BrowserWindow(BrowserWindowOwner& owner, Kind kind);
  ~BrowserWindow() override;
  BrowserWindow(const BrowserWindow&) = delete;
  BrowserWindow& operator=(const BrowserWindow&) = delete;

  // Creates the window, not yet shown. `content_width` and
  // `content_height`: the page area's size for popups (0: default size).
  bool Create(int content_width = 0, int content_height = 0);
  void Show();
  void Activate();
  // The theme setting changed (004): Brouk's window parts follow.
  void RefreshTheme();
  // Settings the tabs' renderers follow (the JSON viewer, 006).
  void SendRendererConfig(bool json_viewer);
  // Opens a tab with `url` at the end of `profile`'s group (empty: the
  // default profile); an empty tab, with the address bar focused, if `url`
  // is empty.
  void OpenTab(const std::string& url,
               bool foreground,
               const ProfileId& profile = ProfileId());
  // A tab for a popup or link from `opener` (in this or another window), in
  // its profile, after the opener if it is here. The caller creates its
  // browser.
  Tab* InsertTabFromOpener(const Tab* opener, bool foreground);
  // Opens an internal page (brouk://...) in a tab, in place of an empty
  // active tab; an active tab already showing it is reused (only the
  // fragment changes). Help, settings and profiles open their own windows.
  void OpenInternalPage(const std::string& url);
  // A page chosen on the history page in tab `from` (013): in place of the
  // history tab when it is in `profile` (empty: the tab's own), else, or
  // with `new_tab`, as a new tab of `profile`.
  void OpenPageFromTab(TabId from,
                       const ProfileId& profile,
                       const std::string& url,
                       bool new_tab);
  // A tab's profile (the default profile if it is gone), and any web
  // browser of `profile` here (for the DevTools protocol).
  ProfileId ProfileOfTab(TabId id);
  CefRefPtr<CefBrowser> AnyBrowserInProfile(const ProfileId& profile) const;
  // Downloads (014): the toolbar button follows the app's list (shown once
  // something was downloaded in this run, with the progress of running
  // downloads and a dot for finished ones not seen yet); Retry on the
  // downloads page starts a download again with a browser of its profile,
  // or loads its address in a new tab.
  void DownloadsChanged();
  void RetryDownload(const DownloadEntry& entry);
  // Removes a tab whose browser never came (the popup was aborted).
  void RemoveUncreatedTab(TabId id);
  Tab* FindTab(TabId id);
  // Profiles (005): the number of tabs of `profile` here, closing them
  // (the profile is deleted), and following changed names, colours or the
  // default profile.
  int TabCountInProfile(const ProfileId& profile) const;
  void CloseTabsInProfile(const ProfileId& profile);
  void ProfilesChanged();
  HWND hwnd() const { return window_; }
  bool is_normal() const { return kind_ == Kind::kNormal; }
  bool closing() const { return closing_; }
  // Unique in this run: where a closed tab came from (013).
  std::int64_t window_id() const { return window_id_; }
  // A closed tab back at its place in its profile group (013).
  void RestoreClosedTab(const ClosedTab& closed, bool activate);

  // Session restore (015). The window as the session saves it (nullopt for
  // popups and closing windows), and bringing a saved one back before
  // Show(): only the active tab loads, the others when first shown.
  std::optional<SessionWindow> SessionState() const;
  void Restore(const SessionWindow& saved);
  // Only one new, empty tab (the start's window, which Restore replaces).
  bool HasOnlyNewTab() const;
  void ShowRestorePagesBar(std::function<void()> restore);

  // TabHost:
  void OnTabChanged(Tab& tab) override;
  void OnTabClosed(Tab& tab) override;
  std::optional<PopupHost> OnTabPopup(Tab& opener,
                                      const PopupRequest& request) override;
  void OnTabPopupAborted(Tab& opener, int popup_id) override;
  void OnTabFullscreenChanged(Tab& tab, bool fullscreen) override;
  void OpenUrlFromTab(Tab& opener,
                      const std::string& url,
                      OpenDisposition disposition) override;
  bool OnTabKey(const CefKeyEvent& event) override;
  TabClientDelegate::SearchEngine TabSearchEngine() override;
  int FaviconSize() override;
  HistoryService* TabHistory() override;
  void OnTabHistoryChanged(Tab& tab) override;
  DownloadService* TabDownloads() override;
  bool TabAskWhereToSaveDownloads() override;
  DownloadTarget TabReserveDownloadPath(const std::string& suggested,
                                        const ProfileId& profile) override;
  void TabReleaseDownloadPath(const std::string& path) override;

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  static LRESULT CALLBACK AddressProc(HWND window,
                                      UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam,
                                      UINT_PTR id,
                                      DWORD_PTR data);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

  // The custom title bar.
  LRESULT OnNcCalcSize(WPARAM wparam, LPARAM lparam);
  LRESULT OnNcHitTest(LPARAM lparam);
  void OnNcButton(UINT message, WPARAM hit_code);
  void OnMouse(UINT message, POINT point);
  int ResizeBorderHeight() const;

  void CreateControls();
  void UpdateFonts();
  void Layout();
  void Paint();
  void PaintToolbar(HDC dc);
  void DrawToolbarButton(const DRAWITEMSTRUCT& item);
  // The downloads button: its glyph, a progress line under it while
  // downloads run, a dot for unseen finished ones.
  void DrawDownloadsButton(const DRAWITEMSTRUCT& item);
  // The downloads page (brouk://downloads, Ctrl+J); everything is seen.
  void ShowDownloads();
  // The indicators at the right of the address bar: zoom ("110 %", click
  // resets to 100 %) and "Pop-ups blocked" (click lists them).
  void DrawIndicator(const DRAWITEMSTRUCT& item);
  void UpdateIndicators();
  // Chrome's "Pop-ups blocked" bubble as a menu: open one, or allow pop-ups
  // for the site from now on.
  void ShowBlockedPopups();
  // The "⋮" menu (Chrome's "Customise and control"): new tab and window,
  // help, licences, exit.
  void ShowAppMenu();
  // Chrome's tab menu (right-click on a tab).
  void ShowTabMenu(TabId id, POINT screen_point);
  // A new tab right after `after` with `url` (empty: a new, empty tab).
  void OpenTabAfter(TabId after, const std::string& url);
  // A tab dropped on another profile's group (005): asks, then reopens its
  // address in that profile and closes it.
  void MoveTabToProfile(TabId id, const ProfileId& profile);
  // The profile group a dragged tab is over, if not its own.
  std::optional<ProfileId> DropTarget(POINT point) const;
  RECT StripBounds() const;
  RECT ToolbarBounds() const;
  RECT ContentBounds() const;
  int Scale(int dips) const;
  void InvalidateStrip();
  void LayoutStrip();
  // A click on a profile group's label: collapses or expands it, or shows
  // its tabs when it was collapsed for lack of room.
  void ToggleGroup(const ProfileId& profile);

  // After tabs were added, removed, moved or activated: shows the active
  // tab and updates the strip, toolbar and window title.
  void ModelChanged();
  // Focuses the active tab's page, or the address bar for an empty tab.
  void FocusActiveTab();
  Tab* ActiveTab();
  void CloseTab(TabId id);
  // The tab as Reopen Closed Tab needs it; `index` is its model index.
  ClosedTab ClosedTabFor(const Tab& tab, std::size_t index) const;
  void UpdateToolbar();
  void UpdateSpinner();

  // Full screen (F11, or a page's HTML full screen, 012): the window covers
  // its monitor without frame, tab strip or toolbar.
  void SetFullscreen(bool fullscreen);
  // "Press Esc to exit full screen" for a few seconds, as in Chrome.
  void ShowFullscreenHint(const wchar_t* text);

  // Browser shortcuts, from the page, the address bar or the window.
  bool HandleShortcut(int key, bool control, bool alt, bool shift);
  void FocusAddressBar();
  void NavigateToAddressBarText();
  void ShowAddress();
  // Sets the address field's text without it counting as typing, with the
  // selection from `select_from` to the end (-1: the caret at the end).
  void SetAddressText(const std::wstring& text, int select_from = -1);
  // Loads `url` from the address bar in the active tab; `typed` counts for
  // history ranking (addresses, not searches; Chrome).
  void NavigateActiveTab(const std::string& url, bool typed);

  // Address bar suggestions (003), as on macOS: the field's text changed by
  // typing; asking the sources again (the profile's history, the search
  // engine); showing the list with the first row's inline completion; the
  // keys the field passes on while the list is open (Up, Down, Escape,
  // Shift+Delete); running a row as Return does.
  void OnAddressEdited();
  void UpdateSuggestions();
  void ShowSuggestions();
  void CloseSuggestions();
  bool HandleSuggestionKey(int key, bool shift);
  void OpenSuggestionRow(int row);

  BrowserWindowOwner& owner_;
  const Kind kind_;
  const std::int64_t window_id_;
  // Closed tabs (013): the window's tabs when it started closing as a
  // whole (one entry), tabs replaced by another (moved to a profile) and
  // profiles being deleted, whose tabs are not kept.
  std::vector<ClosedTab> closed_snapshot_;
  std::set<TabId> replaced_tabs_;
  std::set<ProfileId> discarded_profiles_;
  TabStripModel model_;
  std::map<TabId, std::unique_ptr<Tab>> tabs_;
  // Popups being created from this window's tabs: (opener, popup id) to
  // the window and tab that will hold them.
  std::map<std::pair<TabId, int>, std::pair<BrowserWindow*, TabId>>
      pending_popups_;

  HWND window_ = nullptr;
  HWND back_ = nullptr;
  HWND forward_ = nullptr;
  HWND reload_ = nullptr;
  HWND menu_ = nullptr;
  HWND downloads_ = nullptr;
  HWND zoom_ = nullptr;
  HWND popups_ = nullptr;
  // Tab titles and the strip's buttons on hover.
  HWND tooltip_ = nullptr;
  std::wstring tooltip_text_;
  HWND address_ = nullptr;
  HFONT text_font_ = nullptr;
  HFONT icon_font_ = nullptr;
  int text_height_ = 0;
  RECT address_field_ = {};  // The rounded field behind the address edit.
  HBRUSH field_brush_ = nullptr;
  Theme theme_;
  TabStrip strip_;

  TabStrip::Hit hover_;
  TabStrip::Hit pressed_;
  // A tab being dragged to another place in the strip.
  std::optional<TabId> drag_tab_;
  POINT drag_start_ = {};
  bool dragging_ = false;
  bool tracking_mouse_ = false;
  bool active_ = true;
  // Where the keyboard focus was when the window was deactivated (the
  // address bar, a find bar, a dialog over a page, ...), to go back there.
  HWND saved_focus_ = nullptr;
  bool closing_ = false;
  // Address bar suggestions (003). Answers carry suggest_generation_, so
  // late answers for older text are dropped.
  std::unique_ptr<SuggestionsPopup> suggestions_;
  std::wstring typed_text_;
  // The last edit added text at the end: inline completion is allowed.
  bool allow_inline_ = false;
  bool setting_address_ = false;
  bool suggesting_ = false;
  std::uint64_t suggest_generation_ = 0;
  std::vector<HistoryMatch> history_matches_;
  std::vector<std::string> search_suggestions_;
  std::vector<AddressSuggestion> suggestion_rows_;
  CefRefPtr<SearchSuggestRequest> suggest_request_;
  // Expires with the window, for answers that come later.
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
  bool spinner_running_ = false;
  bool fullscreen_ = false;
  // The window went full screen because a page asked (not F11), and leaves
  // it with the page.
  bool fullscreen_for_page_ = false;
  std::optional<TabId> fullscreen_tab_;
  // How Show() shows the window (maximised if restored so, 015).
  int show_command_ = SW_SHOWNORMAL;
  // To restore the window after full screen.
  WINDOWPLACEMENT saved_placement_ = {};
  LONG_PTR saved_style_ = 0;
  std::unique_ptr<TabPanel> fullscreen_hint_;
  int spinner_step_ = 0;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_BROWSER_WINDOW_H_
