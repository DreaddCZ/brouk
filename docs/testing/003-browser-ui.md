# 003 – Browser UI: manual test checklist

Quick way to get many tabs: serve a few pages and open them from the command
line (each argument opens as a tab):

```sh
mkdir -p /tmp/brouk-tabs && for i in $(seq 1 30); do echo "<title>Tab $i</title><h1>$i</h1>" > /tmp/brouk-tabs/t$i.html; done
python3 -m http.server 8766 --directory /tmp/brouk-tabs &
build/debug-x86_64/Brouk.app/Contents/MacOS/brouk $(for i in $(seq 1 30); do echo http://localhost:8766/t$i.html; done)
```

## Pinned tabs

1. Right-click a tab → **Pin Tab**. Expected: it moves to the start of its profile group, keeps its close button, and shows a small pin on its icon and a faint background; its tooltip ends with "(pinned)".
2. Pin a second tab. Expected: it goes right after the first pinned tab.
3. Tab menu → **Unpin Tab** on the active pinned tab. Expected: it moves to the first place after the remaining pinned tabs. The menu item says "Pin Tab" again.
4. Drag a normal tab to the left of a pinned tab. Expected: it stops after the pinned tabs. Drag a pinned tab to the right past normal tabs: it stays among the pinned tabs.
5. Click a link with ⌘ in a pinned tab. Expected: the new tab opens after the pinned tabs, not pinned.
6. Pin a tab in a second profile's group. Expected: it sits at the start of that group, not of the strip.

## Tab menu

14. Right-click a tab → **New Tab to the Right**. Expected: an empty tab opens right after it, in the same profile, with the address bar focused.
15. **Duplicate**. Expected: the same address opens in a new tab right after it, same profile.
16. In a group with a pinned tab and five other tabs, right-click the third normal tab → **Close Tabs to the Right**, then **Close Tabs to the Left**, then **Close Other Tabs**. Expected: only tabs of that group close, the pinned tab always stays, and tabs of other profiles' groups are untouched. Items with nothing to close are greyed out.
17. A page with a "Leave site?" prompt among the closed tabs. Expected: the prompt appears for that tab; the others close.

## Tabs that do not fit

7. Open about 15 tabs in one window and make the window narrower. Expected: tabs shrink to icon plus close button; the close button stays visible and separated by a divider.
8. With one pinned tab, shrink the window until the other tabs are icon-only. Expected: the pinned tab is still wider (icon plus the start of its title), until there is no room left.
9. Open tabs in two profiles (e.g. 12 in the default profile and 8 in another), keep a tab of the smaller group active, and narrow the window. Expected: the other group collapses on its own (outlined label, its tab count still shown); widening the window expands it again.
10. Click the label of a group that collapsed on its own. Expected: Brouk switches to that group's most recently used tab; the other group may now collapse instead.
11. Open 30 tabs in one group and keep the window narrow. Expected: the pinned tabs, the active tab and the tabs after it are shown, and a "+N" button appears before the group's "+". Clicking "+N" lists the hidden tabs; choosing one switches to it.
12. Activate the last tab in that state. Expected: the tabs before it fill the space.
13. Collapse and expand a group by clicking its label (as before). Expected: unchanged behaviour.

## Shortcuts

18. Open Settings (⌘,) → Shortcuts. Expected: every browser action is listed with its shortcut (e.g. New tab `cmd+t`, Next tab `opt+cmd+right, cmd+}, ctrl+tab`).
19. Click the "Pin or unpin tab" field and press ⌘E. Expected: the field shows `cmd+e` and is saved; Tab → Pin Tab now shows ⌘E and ⌘E pins the current tab.
20. Set "Reload" to ⌘E too. Expected: the settings page shows a problem ("⌘E is set for both …"), the toolbar shows the settings problem button, and ⌘E reloads (the first action keeps it).
21. Set "Print" to `cmd+shift+p`. Expected: ⇧⌘P prints, ⌘P no longer does. Set it to `none`: no menu shortcut.
22. Type `t` or `shift+x` into a shortcut field and leave it. Expected: an error next to the field; the old value stays.
23. Change "Close tab" to `cmd+e`, open a page that captures keys (e.g. a text field on any site) and press ⌘E. Expected: the tab closes (reserved shortcuts win over pages); ⌘W no longer closes the tab.
24. Edit `~/Library/Application Support/Brouk/settings.yaml` by hand (e.g. `shortcuts: { reload: "cmd+e" }`) while Brouk runs. Expected: the menu changes at once.
25. Hover the Back, Forward, Reload and developer tools buttons. Expected: the tooltips show the current shortcuts and update after a change.
26. Change "Developer tools on/off" (006). Expected: the new shortcut toggles dev tools; ⌥⌘I no longer does.
27. Reset everything: delete the `shortcuts:` section from `settings.yaml`. Expected: Chrome's shortcuts again.

## Command palette

28. Press ⌘K in a browser window. Expected: a search panel appears at the top of the window with the field focused, listing the open tabs first.
29. Type `rel`. Expected: "Reload" (with ⌘R at the right) is near the top; Return reloads the page and closes the panel.
30. Open tabs in two windows and two profiles; type a word from a tab title in the other window. Expected: the tab is listed with its profile name and address; Return brings that window to the front and switches to the tab.
31. Type a word without accents that a tab title has with accents (e.g. `prehled` for "Přehled"). Expected: the tab is found.
32. Type `theme`. Expected: the "Theme" setting is listed ("Settings · Appearance"); Return opens Settings scrolled to it, highlighted and focused. Try a profile setting (`colour`): the page shows the active tab's profile.
33. Up/Down move the selection, clicking a row runs it, Escape closes the panel, clicking outside closes it.
34. Type `back` on a new tab with no history. Expected: "Back" is not listed (it cannot run now).
35. Tab menu: New Tab to the Right, Duplicate Tab, Close Other Tabs / to the Left / to the Right act on the active tab like the tab's right-click menu; the close items are greyed out when there is nothing to close.
36. Remap the palette (Settings → Shortcuts → Command palette). Expected: the new shortcut opens it.

## History and address bar suggestions (with 013)

Settings are in Settings → Search ("Suggest pages from your history", "Show search suggestions") and Settings → History ("Keep history for").

37. Visit a few sites by typing their address (e.g. `github.com`, `example.com`) and by clicking links. Then type `git` in a new tab. Expected: a list opens under the address bar; the field shows `git` with `hub.com` completed and selected, and the first row is the GitHub page. Return opens it.
38. Type `gi`, then press ⌫. Expected: no inline completion after deleting (as in Chrome); typing a letter again brings it back.
39. Type words from a page title (e.g. part of a Jira issue title), in any case and without accents. Expected: the page is listed with its title and address.
40. Type a search term (`brouk`). Expected: first row "brouk – Search Google", then history pages if any, then search suggestions from Google. Up/Down move through the rows and the field shows each one; Return opens the selected row.
41. Escape once: the list closes and the completion disappears; Escape again: the field shows the page address.
42. Select a history row with Up/Down and press ⇧⌫. Expected: the row disappears and does not come back for the same text (removed from the profile's history).
43. Use two profiles: visit a site in profile A only, then type its name in a tab of profile B. Expected: not suggested in B.
44. Change the search engine to DuckDuckGo: suggestions come from DuckDuckGo; with a custom engine there are no search suggestions. Turn off "Show search suggestions": only history rows; turn off "Suggest pages from your history": only search rows.
45. Type `https://intranet.example/x`. Expected: no search suggestions are requested for an address with a scheme.
46. Visit a page that redirects (e.g. `http://github.com` → `https://github.com/`). Expected: only the final address is suggested. A site that changes the address without loading (most single-page apps, e.g. Jira) records each page.
47. Error pages (e.g. `http://localhost:1/`), `brouk://settings` and `data:` pages are never suggested.
48. Quit and restart Brouk. Expected: suggestions still come from the history; the profile folder contains `history.db`.

