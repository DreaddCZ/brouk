# 011 – Bookmark manager

- **Status:** ready
- **Priority:** P1
- **Depends on:** 001, 003, 005, 010

## Goal

Save, organise and quickly open frequently used pages. Keep it simple, like Chrome's bookmarks. Add one work-specific feature: a bookmark can be linked to a profile, so it always opens in that profile and does not clutter the others.

## Description

### Structure

- There is **one bookmark tree for the whole app**, not one per profile. It has Chrome's two top-level folders: **Bookmarks bar** and **Other bookmarks**. They cannot be renamed, moved, deleted or linked to a profile.
- Folders can be nested to any depth.
- A bookmark has a name and a URL. A folder has a name.

### Linking to a profile

- Every bookmark and folder is either **general** or **linked to one profile**.
- **A folder's link applies to everything inside it.**
  - An item inside a linked folder has no link of its own; it follows the folder.
  - Moving an item into a linked folder makes it follow that folder.
  - Moving it out again makes it general.
- The *effective profile* of an item is the link of the nearest linked folder above it, otherwise its own link.
- **Visibility:** an item linked to profile A is shown only where the active tab belongs to profile A. Where the active tab belongs to any other profile, it is hidden everywhere: in the bookmarks bar, the Bookmarks menu, address bar suggestions and the command palette.
  - General items are shown in every profile.
  - A general folder whose items are all hidden shows as empty, as Chrome shows an empty folder.
- **Opening:**
  - A general bookmark opens in the profile of the tab it is opened from.
  - A linked bookmark always opens in its profile. When it is opened from somewhere that is not in that profile (e.g. the bookmark manager page), the destination window is chosen as in 010.
- **Deleting a profile:**
  - Bookmarks and folders linked to it become general; nothing is deleted.
  - The profile deletion dialog says how many bookmarks this affects.

### Bookmarks bar

- The bar sits below the toolbar (address bar).
- **By default it is shown only on the new tab page** and hidden on tabs with a loaded page, as in Chrome.
  - ⌘⇧B (and View → Always Show Bookmarks Bar) switches to showing it on every tab, and back.
- It shows the contents of the "Bookmarks bar" folder that are visible in the active tab's profile. Folders open as drop-down menus.
- Items that do not fit go into a "»" overflow menu at the right end, as in Chrome.
- **Mouse actions:**
  - Click opens the bookmark in the current tab.
  - ⌘-click or middle-click opens it in a new tab.
  - ⌘⇧-click opens it in a new tab and switches to it.
- **Drag and drop:**
  - Drag to reorder items, or to move them into and out of folders.
  - Drop a link or the address bar's site icon onto the bar to bookmark it there.
- **Right-click menu:** Open in New Tab, Open in New Window, Edit…, Delete, Add Bookmark…, Add Folder…, Bookmark Manager, Always Show Bookmarks Bar.
  - On a folder: Open All (n) instead of the two Open items.

### Bookmarks menu

The menu bar has a **Bookmarks** menu, as in Chrome. It contains:

- Bookmark This Tab… (⌘D)
- Bookmark All Tabs… (⌘⇧D) — saves the tabs of the active tab's profile group into a new folder
- Bookmark Manager (⌥⌘B)
- Always Show Bookmarks Bar (⌘⇧B)
- The bar's contents, with folders as submenus
- Other bookmarks as a submenu

Only items visible in the active tab's profile are listed.

### Adding and editing a bookmark

- A **star button** at the right end of the address bar bookmarks the current page with one click; ⌘D does the same.
  - The star is filled when the page's URL is already bookmarked by an item visible in the active tab's profile.
- Clicking the star (or ⌘D) opens a dialog anchored to the star with:
  - **Name** — the page title, editable.
  - **Folder** — the last used folder; can be changed, or a new folder created in place.
  - **Profile** — "General" or any profile. It is preselected to the active tab's profile.
    - When the chosen folder is linked to a profile, this field shows that profile and cannot be changed.
- The new bookmark is saved when the dialog opens, as in Chrome.
  - **Done** keeps the changes.
  - **Remove** deletes the bookmark.
  - Closing the dialog keeps it.
- On a page that is already bookmarked, the same dialog edits that bookmark.
- Editing from the bar, the menu or the manager page uses a dialog with name, URL, folder and profile. For a folder, the dialog has name and profile.

### Bookmark manager page

- An internal HTML page (`brouk://bookmarks`), opened from the Bookmarks menu, ⌥⌘B or the bar's right-click menu.
- It shows **all** bookmarks of all profiles:
  - a folder tree on the left and the selected folder's contents on the right;
  - every item linked to a profile shows that profile's colour and name;
  - a **profile filter** (All / General / one profile) and a **search** box (matching name and URL).
- **Actions:** add bookmark, add folder, edit, delete, move by drag and drop, multi-select (⌘-click, ⇧-click), and delete or move several items at once.
- Opening a bookmark from the page follows the opening rules above.
- **Undo:** deleting a bookmark or folder, on the page or anywhere else, shows an "Undo" option for a few seconds; ⌘Z on the page also undoes it.

### Search

Address bar suggestions (003) and the command palette include bookmarks visible in the active tab's profile, matched by name and URL.

### Import and export

- The manager page can **export** all bookmarks to the standard HTML bookmark file, and **import** such a file.
  - An import goes into a new folder "Imported" under Other bookmarks, as general items.
  - The HTML format has no profile links, so they are not exported.
- **Profile export (005)** includes bookmarks and folders whose effective profile is that profile, with their folder path.
  - On import, they are recreated under the same path. Missing folders are created as general folders.
  - They are linked to the imported profile.

### Storage

Our own implementation in the core; CEF (Alloy style) has no bookmark system. One JSON file for the whole app, `bookmarks.json` in the app data folder (on macOS `~/Library/Application Support/Brouk/`). The file is written atomically, and a backup of the previous version is kept, as Chrome does. Every item has a stable internal ID; a link refers to a profile by its ID, not its name.

Per the hybrid UI decision in 001: the bar, the menu, the star and its dialog, and the edit dialog are native UI; the manager page is HTML/TypeScript.

## Acceptance criteria

**Adding and editing**
- [ ] Clicking the star or pressing ⌘D bookmarks the current page and opens a dialog to change its name, folder and profile; Remove deletes it again.
- [ ] In that dialog, the profile is preselected to the active tab's profile and can be changed to General or any other profile.
- [ ] The star is filled on a page bookmarked by an item visible in the active tab's profile, and clicking it then edits that bookmark.
- [ ] Bookmarks and folders can be added, renamed, edited (name, URL, folder, profile) and deleted from the bar, the menu and the manager page.
- [ ] Any deletion can be undone.

**Structure and linking**
- [ ] Folders can be nested and items reordered or moved by drag and drop, both on the bar and on the manager page.
- [ ] A folder linked to profile A makes everything inside it linked to A; in the dialogs, such items show A and their profile cannot be changed.
- [ ] Moving an item into a folder linked to A makes it linked to A; moving it out to a general folder makes it general.

**Visibility and opening**
- [ ] An item linked to profile A appears in the bar, the Bookmarks menu, address bar suggestions and the command palette only when the active tab belongs to A; general items appear in every profile.
- [ ] A general bookmark opens in the profile of the tab it is opened from.
- [ ] A linked bookmark opened from the manager page in another profile's tab opens in its own profile, in the window chosen by the rules of 010.

**Bookmarks bar and menu**
- [ ] By default the bar is shown on the new tab page and hidden on tabs with a loaded page.
- [ ] ⌘⇧B switches between "new tab page only" and "always shown"; the choice is kept after restart.
- [ ] Click, ⌘-click, middle-click and ⌘⇧-click on the bar behave as described; items that do not fit are in the "»" menu.
- [ ] Dropping a link or the address bar's site icon onto the bar creates a bookmark there.
- [ ] The Bookmarks menu lists the bar's contents and Other bookmarks, with folders as submenus, and has Bookmark This Tab, Bookmark All Tabs, Bookmark Manager and Always Show Bookmarks Bar.

**Manager page**
- [ ] The manager page shows all bookmarks of all profiles, marks linked items with their profile's colour and name, and can filter by profile and search by name and URL.
- [ ] The manager page supports multi-select, and deleting or moving several items at once.

**Profiles, import and storage**
- [ ] Deleting a profile turns its linked bookmarks and folders into general ones; the deletion dialog says how many there are.
- [ ] All bookmarks can be exported to and imported from the standard HTML bookmark file; imported items land in "Imported" under Other bookmarks, as general items.
- [ ] A profile export contains that profile's linked bookmarks; importing it recreates them in their folders, linked to the imported profile. (Extends the export from 005.)
- [ ] Bookmarks survive a restart and a crash during saving (atomic write, previous version kept).

## Out of scope

- Sync between devices.
- Sharing bookmark folders with other people.
- Tags, keywords (e.g. `jira 123`), and notes or descriptions on bookmarks.
- Direct import from installed Chrome, Firefox or Safari (in line with 005); only the HTML file.
- Linking one bookmark to several profiles.
- Bookmark icons fetched for sites never visited. Icons come from the profile's own favicon cache; if there is none, a generic icon is shown.

## Configuration

| Setting | Default | Scope |
|---|---|---|
| Show bookmarks bar | new tab page only (alternative: always) | global |
| Include bookmarks in address bar suggestions | on (already in 003) | global |

The folder in the add dialog is the last used folder (Chrome's behaviour), so there is no setting for it.

## Open questions

None.

## Notes

- 2026-10-22 (bookmarks review with the user): the earlier draft had separate bookmarks per profile. It was replaced by one shared tree with optional links to a profile. Folders can be linked too, and their contents follow them. The add dialog preselects the active tab's profile. Deleting a profile makes its linked bookmarks general. The bar is shown on the new tab page only, with ⌘⇧B for "always". Tags, keywords, notes and direct import from other browsers were dropped to keep it simple. Storage is a JSON file, like Chrome's. Spec set to `ready`. 005 and 003 were updated to match.
- Ordering behaviour not specified here (e.g. where a new bookmark goes in a folder, which is at the end) follows Chrome.
