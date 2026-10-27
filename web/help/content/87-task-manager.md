## Task manager {#task-manager}

Shows **which page uses the CPU, memory or network**, and lets you deal with it. macOS Activity Monitor lists Brouk's helper processes only as "Brouk Helper (Renderer)"; the task manager tells you which tab each one is.

**Open it:** Window → Task Manager (<kbd>⇧⌘⎋</kbd>, `shortcuts.task_manager`), the command palette, or right-click a tab → **Show in Task Manager**, which selects that tab's row. There is one task manager window; it remembers its size and position. While it is closed, Brouk measures nothing.

**What it lists:** every tab of every window and profile, Brouk's own pages (settings, help…), the workers pages run, and Brouk's browser, GPU and helper processes. Rows that run in **one process** are grouped: a line separates processes, and the rows after a group's first one are indented. Ending such a process affects all of its rows. Tabs that have not loaded yet, or were unloaded, show as **not loaded**, without values.

**Columns:** Task, Profile, Memory, CPU, Status and Process ID are shown; right-click the header for more: GPU memory, JavaScript memory, Network, Address, Type, Shared with and Window.

- **Memory** is the process's memory footprint, the value Activity Monitor shows as "Memory". **CPU** is the process's use, where 100 % is one full core. Both are shown on a process's first row, as they belong to the whole process.
- **Process ID** matches Activity Monitor and `top`. Tabs report it; the GPU and helper processes have none.
- **Network** is the data a tab received per second, counted by Brouk. It is approximate: data from the cache or a service worker may be missing.
- **Status** shows: playing audio, muted, camera, microphone, screen sharing, paused, loading, crashed, not loaded and DevTools open.
- Click a header to sort, again to reverse. Processes stay together, ordered by their value. A **total** at the bottom shows Brouk's whole memory and CPU.

Brouk remembers the columns and sort order (`task_manager.columns`, `task_manager.sort`). Values update every second; Settings → [Task manager](#settings-task-manager) changes that and how long the graph goes back.

**Actions** work on the selected rows (click, <kbd>⌘</kbd>-click, <kbd>⇧</kbd>-click; arrow keys move). For a worker or subframe they act on its tab.

| Action | Key | What it does |
|---|---|---|
| Go to tab | <kbd>Return</kbd>, double-click | Brings the tab's window forward, on its Space or display, and selects the tab. |
| End process | <kbd>⌘⌫</kbd> | Stops the process at once. Its tabs show the crash page with a Reload button. If other tabs share the process, Brouk names them and asks first. The browser and GPU processes can't be ended. |
| Pause / Resume | <kbd>P</kbd> | Freezes the page: scripts, timers and media stop and it uses no CPU, but it keeps its state. |
| Unload tab | <kbd>U</kbd> | Frees the tab's memory. The tab stays with its title and loads again when you switch to it; form contents and page state are lost. The tab shown in its window can't be unloaded. |
| Mute / Unmute | <kbd>M</kbd> | Mutes the tab. |
| Reload | <kbd>R</kbd> | Reloads the tab. |
| DevTools | <kbd>D</kbd> | Switches the tab's developer tools on. |

**A paused tab** stays paused when you switch to it, so you can look at the page in that state. Its tab shows a pause icon and a dimmed title, and a bar on the page says "This page is paused" with a **Resume** button. Right-click the tab for Pause Tab / Resume Tab too. Loading a page, reloading or going back resumes it; after a restart, tabs start unpaused.

**Pause updates** freezes the table and the graph, so you can read or copy values that keep changing. **Copy CSV** and **Copy JSON** copy the selected rows, or all rows shown, with the time of copying; JSON also has the graph's values of each row. Paste them into a bug report or a spreadsheet.

**The graph** below the table shows CPU (solid line, left scale) and memory (dashed, right scale) of the selected rows over the last 5 minutes, one colour per row; point at it for exact values. Rows of one process show the process's values. Brouk keeps these values only while the task manager is open. Click "Graph of the selected rows" to hide or show the graph.

The filter field narrows the rows by title, address or profile.
