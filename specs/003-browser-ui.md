# 003 – Browser UI

- **Status:** in-progress
- **Priority:** P1
- **Depends on:** 001, 004, 005

## Goal

A clean, minimal browser interface built for focused work, which the user can rearrange and control from the keyboard.

## Description

Core UI elements:

- Windows with tabs, an address/search bar, back/forward/reload.
- **Address bar search:** text that is not a URL is searched with the default search engine — **Google** by default. The search engine is configurable: choose from a built-in list (Google, Bing, DuckDuckGo, Startpage, Ecosia) or add a custom one with a URL template (`https://example.com/search?q=%s`).
- **Address bar suggestions,** in this order: (1) **history** of the active tab's profile, (2) **bookmarks** visible in that profile (general ones and those linked to it, see 011), (3) **search suggestions** from the search engine, last. Each source can be turned off; search suggestions send what the user types to the search engine (as in Chrome) and are on by default.
- Minimal chrome by default: no promotional content, news feed or unnecessary buttons.
- Keyboard-first: every action reachable by a shortcut; shortcuts remappable.
- A command palette (search for any action, tab, bookmark or setting).
- Horizontal tab strip. Vertical tabs come later (see Out of scope).
- Tab groups: each profile's tabs in a window form one group, labelled with the profile's name, colour and number of open tabs (see 005). Clicking the group label collapses or expands the group, as in Chrome. Each group has its own "+" button that opens a new tab in that group.
- The default profile's group is always present in every window, even with no tabs, at the far right of the tab strip (at the bottom with vertical tabs later). Other profiles' groups appear when they get their first tab and disappear when their last tab closes.
- Pinned tabs work as in Chrome, with these differences: a pinned tab sits at the start of its own profile group (not outside the groups), it keeps its close button, and it stays wider than other tabs when space is short — e.g. icon plus the first few letters of its title while other tabs are icon-only.
- When tabs do not fit:
  1. They shrink down to just their icon.
  2. If there is still not enough space, other groups collapse automatically to make room for the group the user is working in.
  3. If one group alone still does not fit, it shows the current tab, its pinned tabs and the 3 tabs after the current one, and collapses the rest. This step is an edge case; details will be tuned during testing.
- The close button (✕) stays visible on every tab at every width and is visually separated from the rest of the tab, so the user does not close a tab by accident when trying to drag it.
- Light and dark theme following the OS by default.
- Clear indication of the profile: every tab shows its profile group, and the toolbar shows the active tab's profile colour.
- Bookmarks, history and downloads views — specified separately in 011, 013 and 014 (later).

## Acceptance criteria

- [ ] Basic browsing: open, close, reorder and switch tabs; navigate; reload; back/forward.
- [ ] All toolbar actions have a keyboard shortcut, and shortcuts can be changed in settings.
- [ ] Command palette opens with a shortcut and can run actions and switch tabs.
- [ ] Tabs are shown in a horizontal strip.
- [ ] When tabs do not fit, they shrink down to their icon; if that is not enough, other groups collapse to make room for the group in use.
- [ ] Every tab's close button is visible at every tab width and visually separated from the tab's drag area.
- [ ] A tab group can be collapsed and expanded by clicking its label; the label shows the number of open tabs.
- [ ] Every group has a "+" button that opens a new tab in that group's profile.
- [ ] Every window always shows the default profile's group at the far right of the tab strip, also when it has no tabs.
- [ ] Tabs can be pinned and unpinned; a pinned tab sits at the start of its profile group and shows a close button.
- [ ] When tabs shrink, pinned tabs stay wider than other tabs while space allows (icon plus the start of the title vs. icon only).
- [ ] Every tab shows which profile it belongs to (its group's name and colour), and the toolbar shows the active tab's profile colour.
- [ ] Theme follows the OS light/dark setting, with manual override.
- [ ] Typing text that is not a URL searches with the default search engine (Google unless changed); the engine can be changed in settings, including a custom URL template.
- [ ] Address bar suggestions list history matches first, then bookmark matches (after 011), then search suggestions; each source can be turned off.

## Out of scope

- Detailed visual design (separate design work).
- Vertical tabs — planned for later, in a separate spec.
- Workspaces (named, switchable sets of tabs) — not planned for now; profile groups cover the need.

## Configuration

Theme, toolbar items, shortcuts, new tab page content, default search engine (Google) and custom search engines, suggestion sources (history, bookmarks, search suggestions — all on).

## Open questions

None.

## Notes

- 2026-10-21 (readiness review): default search engine Google, configurable; suggestions from history first, then bookmarks, search suggestions last. Bookmarks (011), history (013) and downloads (014) views are specified later. Search engine and suggestion settings are global for now (provisional — could become per profile later).
- 2026-10-21 (readiness review): milestone 1 ("browse the web", see roadmap) needs only a minimal subset of this spec: one window with a simple tab strip (no profile groups yet), address bar with search, back/forward/reload, and the standard shortcuts. Build that subset so it grows into the full UI here — no throwaway shell.
- 2026-10-21 (UI and profiles review): left to be resolved during testing, not specified now: the exact behaviour in step 3 of tab overflow, split view, new tab page content, session restore on startup, reopening closed tabs.
- 2026-10-21 (UI and profiles review): pinned tabs sit at the start of their profile group, keep the close button and stay wider than other tabs when space is short. Spec set to `ready`; implementation also needs the configuration decision in 004 (shortcuts, theme and other settings).

- 2026-10-21 (decision in 001): hybrid UI shell. Windows, tabs, toolbar, address bar and menus are native (Objective-C++/AppKit on macOS); settings, history, bookmarks, downloads and other internal pages are HTML/TypeScript pages rendered in CEF. This answers "native look or custom cross-platform look" for the main window chrome; internal pages follow the same theme.
- 2026-10-21 (UI and profiles review): horizontal tabs first, vertical later. Tab groups are profile groups (one group per profile per window, see 005), collapsible like in Chrome. Separate workspaces are not needed for now.
- 2026-10-23 (user's decision): 003 is built next, then 013, then 011. The command palette opens with ⌘K (Chrome has no comparable shortcut on macOS). History for suggestions is stored in SQLite (013), approved as a library by the user. Built in several pull requests: pinned tabs and tab overflow; remappable shortcuts; command palette; history storage and address bar suggestions.
- 2026-10-23 (implementation, pinned tabs and overflow): pinned tabs follow Chrome's rules inside their group: pinning moves a tab to the end of the group's pinned tabs, unpinning to the first place after them, pinned and other tabs cannot be dragged past each other, and links from a pinned tab open as normal tabs after the pinned ones. Pin/Unpin is in the tab's right-click menu and in a new Tab menu, as in Chrome (Select Next/Previous Tab moved there from Window). Overflow (`src/core/tab_strip_layout`): the narrowest tab is icon plus close button; a pinned tab keeps at least ~96 pt (icon and the start of its title) while the others can still shrink. Step 2 collapses the other groups with the most tabs first, only as many as needed; they are drawn like collapsed groups and expand again when space returns; clicking the label of such a group switches to its most recently active tab. Step 3 (to be tuned in testing): the active group shows its pinned tabs, the active tab and as many tabs after it as fit (near the end of the group, tabs before it fill the space); the hidden tabs are listed in a "+N" menu next to the group's "+" button.
- 2026-10-23 (user's decision, testing feedback): pinned tabs show a pin badge on their icon and a faint background. The tab's right-click menu follows Chrome's: New Tab to the Right, Reload, Duplicate, Pin/Unpin, Mute/Unmute, Close Tab, Close Other Tabs, Close Tabs to the Left (not in Chrome, asked for by the user) and Close Tabs to the Right. The "Close …" items act only inside the tab's own profile group and keep pinned tabs (Chrome acts on the whole window). Duplicate opens the same address only: CEF cannot copy the back/forward history.
- 2026-10-23 (implementation, shortcuts): every browser action is a global setting `shortcuts.<action>` (`src/core/shortcuts.cc` lists them with Chrome's macOS defaults), e.g. `shortcuts.new_tab: "cmd+t"`. A value is one or more shortcuts separated by commas (`"opt+cmd+right, cmd+}, ctrl+tab"`), or `none`. Each shortcut needs Cmd or Ctrl (F1–F12 excepted) so it cannot take a typing key; "," and "+" are written `comma` and `plus`. A changed shortcut takes precedence over another action's default, which then loses it. If two changed settings use the same shortcut, the first action in the list keeps it and the conflict is reported with the other settings problems. Chrome's reserved shortcuts (new/close tab or window, switching tabs) stay reserved after remapping: pages cannot block them. Standard macOS items (Quit, Hide, Minimize, Undo, Cut/Copy/Paste, Select All) are not listed, since macOS already lets the user remap them. Toolbar actions without a Chrome shortcut (site information, pin, mute, manage profiles) start with none and can be given one; the toolbar tooltips show the current shortcut. On the settings page, pressing keys in a shortcut field records them; the text can also be typed.
- 2026-10-23 (user's decision): site information, pin, mute and manage profiles keep no default shortcut for now; the user may choose defaults after more testing.
- 2026-10-23 (implementation, command palette): ⌘K (View → Command Palette…) opens a search panel at the top of the browser window. It finds the open tabs of all windows (front window first; shown with profile and address), the browser actions that can run now (with their shortcuts), and every setting on the settings page (opens the page at that setting, for the active tab's profile where the setting is per profile). Bookmarks follow with 011. Matching (`src/core/command_palette`) ignores case and accents; every typed word must match; a match at the start of the title or of a word counts most, then anywhere in the title, then in the detail (address or settings section), then the title's letters in order ("nt" → New tab). Equal matches list tabs, then actions, then settings. With an empty query, tabs come first. Up/Down move, Return runs, Escape or clicking elsewhere closes. New Tab to the Right, Duplicate Tab and Close Other Tabs / to the Left / to the Right are also in the Tab menu as remappable actions, for the active tab.
- 2026-10-23 (implementation, suggestions): typing in the address bar opens a list under it. The first row is what Return does: the typed text as an address or a search ("… – Search Google"), or, when the text starts an address typed before, that page with the rest completed inline (selected, so typing replaces it). Then up to 5 pages from the active tab's profile history (013), the typed text again if a page was completed inline, and up to 4 search suggestions from the engine's suggestion service (Google, Bing, DuckDuckGo, Startpage and Ecosia; none for a custom engine). Up/Down move through the list and show the row in the field, Return or a click opens it, the first Escape closes the list and the second restores the page address, ⇧⌫ removes a history row from the profile's history. Settings: `suggestions.history` and `suggestions.search` (Search section, both on); the bookmarks source follows with 011. Search suggestions are not requested for text with a scheme (`https://…`, `file:`), as in Chrome, and use the tab's profile (its cookies), as Chrome's do.

