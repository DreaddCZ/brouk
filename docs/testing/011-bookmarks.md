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

## Bookmarks bar

20. Open a new tab (⌘T). Expected: the bookmarks bar is shown below the toolbar with the "Bookmarks bar" folder's items; load a page in it: the bar hides.
21. With no bookmarks on the bar, the new tab page shows the hint "For quick access, place your bookmarks here on the bookmarks bar."
22. Press ⇧⌘B (or View → Always Show Bookmarks Bar, which gets a checkmark). Expected: the bar shows on every tab; quit and restart: still shown; ⇧⌘B again: only on the new tab page.
23. Click a bookmark on the bar. Expected: it opens in the current tab. ⌘-click and middle-click: a new background tab. ⇧⌘-click: a new tab that becomes active.
24. Click a folder. Expected: a menu with its items, subfolders as submenus; choosing an item opens it (with ⌘ held: in a new tab).
25. Make the window narrow. Expected: items that do not fit disappear and a "»" button at the right lists them.
26. Switch to a tab of another profile. Expected: bookmarks and folders linked to the first profile disappear from the bar; general ones stay.
27. Drag a bookmark along the bar. Expected: a blue line shows where it goes; dropping moves it there.
28. Drag a bookmark onto the middle of a folder. Expected: the folder highlights; dropping moves the bookmark into it (at the end).
29. Drag a link from a web page onto the bar. Expected: a bookmark with the link's title and address appears where dropped.
30. Drag the site icon (left of the address) onto the bar. Expected: the current page is bookmarked there.
31. Right-click a bookmark: Open in New Tab, Open in New Window, Edit…, Delete, Add Bookmark…, Add Folder…, Bookmark Manager, Always Show Bookmarks Bar. Edit… opens a dialog with name, URL, folder and profile; changes are saved.
32. Right-click a folder: Open All (n) opens its bookmarks in new tabs (more than 15 asks first); Edit… shows name and profile.
33. Delete a bookmark from the right-click menu. Expected: "Bookmark deleted" with Undo; Undo puts it back.
34. Right-click the empty part of the bar → Add Bookmark… Expected: the dialog is filled with the current page; Save adds it at the end of the bar. Add Folder… adds a folder.
