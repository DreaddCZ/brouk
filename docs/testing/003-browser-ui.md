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
