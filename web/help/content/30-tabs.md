## Tabs and windows {#tabs}

- Tabs are grouped by profile ([Profiles side by side](#profiles-groups)).
- **Pinned tabs:** right-click a tab → **Pin**. A pinned tab sits at the start of its profile's group, shows a pin badge, keeps its close button and stays wider than other tabs when space is short.
- **When tabs don't fit,** they shrink to their icon; then other groups collapse to make room for the one you are using. Click a collapsed group's label to go back to it. If one group alone still doesn't fit, a **+N** menu next to its **+** lists the hidden tabs.
- **Tab menu** (right-click a tab): New Tab to the Right, Reload, Duplicate, Pin/Unpin, Mute/Unmute, Move Tab to Another Window, Close Tab, Close Other Tabs, Close Tabs to the Left/Right. The "Close…" commands act only within the tab's profile group and keep pinned tabs.
- A speaker icon on a tab playing sound mutes it.
- **Reopen a closed tab** with <kbd>⇧⌘T</kbd> ([History menu](#history-menu)); on Windows <kbd>Ctrl+Shift+T</kbd>, or "⋮" → Recently closed.
- Duplicating or reopening a tab brings back its page, not its back/forward list.

### Moving tabs and groups {#tabs-dragging}

As in Chrome, tabs move freely between windows; the page moves with them and is not reloaded, so videos keep playing and typed text stays.

- **Reorder profile groups:** drag a group's label left or right. The default profile's group always stays last.
- **Tab to a new window:** drag the tab up or down out of the tab strip and drop it anywhere outside a tab strip. The tab menu's **Move Tab to Another Window** → New Window does the same (macOS: also Tab → Move Tab to New Window).
- **Tab to another window:** drop it on that window's tab strip, where the mark shows, or choose the window in **Move Tab to Another Window**. It joins its own profile's group there, which is created if that window has none. Dropped inside another profile's group, Brouk asks to [move it to that profile](#profiles-groups).
- **A window's only tab** drags the whole window; dropped on another window's tab strip, the tab joins it and the empty window closes.
- **A whole group:** drag its label out of the strip, or right-click the label → **Move Group to New Window** or **Move Group to Another Window**. In another window its tabs join that window's group of the same profile.
- A moved tab keeps pinned, muted, paused, zoom and developer tools; <kbd>Esc</kbd> while dragging cancels. A window left without tabs closes.

### Address bar {#address-bar}

- Clicking the address bar, <kbd>⌘L</kbd>, or a new tab selects the address, so you can type or paste right away.
- Type an address or words to search. The search engine is in Settings → [Search](#settings-search).
- **Suggestions** while typing: your history in this tab's profile, then bookmarks visible in this profile, then your search engine's suggestions. Each source can be turned off in Settings → Search.
- An address you typed before is completed inline; keep typing to replace it.
- <kbd>↑</kbd> <kbd>↓</kbd> move through the list, <kbd>Esc</kbd> closes it (twice restores the page's address), <kbd>⇧⌫</kbd> (on Windows <kbd>Shift+Delete</kbd>) removes a highlighted history suggestion from the history.
- The icons in the address bar: the lock and site information (permissions, certificate), the [shield](#adblock-shield), the [key](#passwords), the star ([bookmarks](#bookmarks)), and the [developer tools toggle](#devtools).

### Command palette {#command-palette}

<kbd>⌘K</kbd> (View → Command Palette…; on Windows <kbd>Ctrl+K</kbd> or "⋮" → Command palette) finds anything by typing a few letters:

- open tabs of all windows,
- browser actions that can run now, with their shortcuts,
- bookmarks visible in this profile,
- every setting (opens Settings at it),
- sections of this help.

Every word you type must match; case and accents don't matter, and initials work ("nt" finds *New tab*). <kbd>↑</kbd> <kbd>↓</kbd> move, <kbd>Return</kbd> runs, <kbd>Esc</kbd> closes.

### Session restore {#session-restore}

When you start Brouk, your windows and tabs come back as they were when you quit — Settings → [Startup](#settings-startup) can open a new tab instead. Tabs in the background load when you first show them. After a crash, a bar offers **Restore pages**.
