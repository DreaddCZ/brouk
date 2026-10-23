# 011 – Bookmarks: manual test checklist

Use at least two profiles (e.g. "Work" and "Home") with tabs in the same window.

## Star, ⌘D and the bubble

1. Open a web page and click the star at the right end of the address bar. Expected: the star fills; a bubble "Bookmark added" shows the page title as name, the last used folder (Bookmarks bar the first time) and the tab's profile.
2. Change the name, pick another folder and press Done. Expected: the bookmark has the new name and is at the end of that folder (Bookmarks menu shows it there).
3. Press ⌘D on the same page. Expected: the bubble says "Edit bookmark" and shows the saved values.
4. In the bubble choose Folder → New Folder…, enter a name, Create. Expected: the folder is created inside the folder selected before and is selected.
5. Change Profile to "General (all profiles)", Done. Switch to a tab of the other profile and open the same address. Expected: the star is filled there too.
6. Change it back to the first profile. In a tab of the other profile with the same address, the star is empty.
7. Choose a folder linked to a profile (see step 12). Expected: Profile shows that profile and is greyed out.
8. Click Remove. Expected: the star empties; a notice "Bookmark removed" with Undo appears for a few seconds. Click Undo: the bookmark is back at its old place.
9. Click outside the bubble instead of Done. Expected: the bookmark is kept with the changes made.
10. On a new tab page and on brouk://history, the star is hidden and ⌘D beeps; Bookmarks → Bookmark This Tab… is greyed out.

## Bookmarks menu

11. Open the Bookmarks menu. Expected: Bookmark This Tab… (⌘D), Bookmark All Tabs… (⇧⌘D), then the bar's bookmarks with folders as submenus, then Other Bookmarks as a submenu (when it has items).
12. Bookmark a page in profile Work, then link a folder to Work (Bookmark All Tabs… creates one, step 15). Switch to a Home tab and open the Bookmarks menu. Expected: Work's bookmarks and folder are not listed; general ones are.
13. Choose a bookmark in the menu. Expected: it opens in the current tab; with ⌘ held, in a new background tab.
14. A bookmark linked to Work, chosen from the menu while a Work tab is active, opens in that tab.
15. Bookmarks → Bookmark All Tabs… Expected: a dialog with name, folder and profile (the active tab's preselected). Save: a new folder with the web pages of the active profile group's tabs (new tab pages and history tabs left out).
16. Remap ⌘D and ⇧⌘D in Settings → Shortcuts. Expected: the menu and the star's tooltip show the new shortcuts.

## Storage

17. Add bookmarks, quit Brouk (⌘Q) and start it again. Expected: all bookmarks are there.
18. In `~/Library/Application Support/Brouk/`, `bookmarks.json` and `bookmarks.json.bak` (the previous version) exist after two changes.
19. Quit Brouk, damage `bookmarks.json` (e.g. delete its last line), start Brouk. Expected: the bookmarks of the previous version are there.
