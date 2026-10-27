## Bookmarks {#bookmarks}

- Click the **star** in the address bar or press <kbd>⌘D</kbd> to bookmark the page. The bookmark is saved at once; the bubble lets you change its name, folder and profile, or **Remove** it.
- The star is filled when the page is bookmarked; clicking it then edits that bookmark.
- **Bookmarks → Bookmark All Tabs…** (<kbd>⇧⌘D</kbd>) saves the tabs of the current profile's group into a new folder.
- Bookmarks appear in [address bar suggestions](#address-bar) and the [command palette](#command-palette).
- Deleting a bookmark anywhere shows **Undo** for a few seconds; <kbd>⌘Z</kbd> on the bookmark manager undoes it too.
- Bookmarks show their site's icon once you have visited the site in that profile.

### Bookmarks and profiles {#bookmarks-profiles}

There is one bookmark tree for the whole app. Each bookmark or folder is either **General** — shown in every profile — or **linked to one profile**: shown only where the active tab belongs to that profile, and always opened in it.

- A new bookmark is linked to the tab's profile; choose *General* in the bubble to see it everywhere.
- A folder's link applies to everything inside it. Moving an item into a linked folder links it; moving it out to a general folder makes it general.
- A general bookmark opens in the profile of the tab you open it from.
- A linked bookmark opened from somewhere else (e.g. the bookmark manager in another profile's tab) opens in its own profile, in the window that profile's tabs are in; Brouk asks which window if there are several.
- Deleting a profile keeps its bookmarks as general ones.

### Bookmarks bar {#bookmarks-bar}

- The bar shows on new tabs; <kbd>⇧⌘B</kbd> (View → Always Show Bookmarks Bar) shows it on every tab.
- Click opens in the current tab; <kbd>⌘</kbd>-click or middle-click in a new tab; <kbd>⇧⌘</kbd>-click in a new tab you switch to; <kbd>⇧</kbd>-click in a new window.
- Folders open as menus; items that don't fit are in the **»** menu.
- Drag items to reorder them or onto a folder to move them in. Drop a link, or the site icon left of the address, onto the bar to bookmark it there.
- Right-click for Open in New Tab/Window, Open All, Edit…, Delete, Add Bookmark…, Add Folder….

### Bookmark manager {#bookmarks-manager}

**Bookmarks → Bookmark Manager** (<kbd>⌥⌘B</kbd>) opens all bookmarks of all profiles as a tab:

- folders on the left, the folder's contents on the right; linked items show their profile's colour and name;
- **Profile** filter (all, general, one profile) and **search** by name and address;
- select with click, <kbd>⌘</kbd>-click, <kbd>⇧</kbd>-click, <kbd>⌘A</kbd>; drag to move; <kbd>Delete</kbd> deletes;
- double-click or <kbd>Return</kbd> opens a bookmark ([which profile](#bookmarks-profiles)).

### Import and export {#bookmarks-import-export}

**Export…** and **Import…** on the bookmark manager use the standard HTML bookmark file of every browser. Imported bookmarks go into a new folder "Imported" under Other bookmarks, as general bookmarks. A [profile export](#profiles-export) carries the bookmarks linked to that profile.
