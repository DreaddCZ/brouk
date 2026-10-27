# Manual test checklist: task manager (020)

Each step names the acceptance criterion it checks.

Serve the test pages first (see [README](README.md)):

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory docs/testing/pages
```

Open `http://127.0.0.1:8765/task-manager.html` in two tabs of one profile
and one tab of another profile, in two windows.

## Window and rows

1. Window → Task Manager. → A "Task Manager" window opens. Choose it again
   from another window. → The same window comes to the front.
   *(opens; one window)*
2. Close it, move and resize it, reopen. → Same size and place, also after
   restarting Brouk.
3. Right-click a tab → Show in Task Manager. → The window opens (or comes
   forward) with that tab's row selected. *(Show in Task Manager)*
4. → Every open tab of both windows and profiles is listed with its title
   and profile (in the profile's colour); "Brouk", "GPU process" and
   "Utility: …" rows are there too; tabs you haven't opened since a
   restart say "not loaded" and show no values. *(every tab; other
   processes; not loaded)*
5. Tabs that share a process (if any; e.g. the same site opened from a
   link) are one group: a line above the group, later rows indented,
   Memory/CPU only on the first row. *(groups)*

## Values

6. In one test tab press **Start a busy loop**. → Within about 3 seconds
   its CPU is near 100 %; click the CPU header → it sorts to the top.
   *(busy loop)*
7. In another test tab press **Allocate 100 MB more** a few times.
   Right-click the header → tick JavaScript memory. → Memory and
   JavaScript memory of that tab grow. *(memory)*
8. Compare the Process ID of a tab with Activity Monitor (View → All
   Processes; search "Brouk Helper (Renderer)", PID column). → The same
   number exists there; ending it in Activity Monitor crashes that tab.
   *(process ID)*
9. Open `audio.html` and play the sound. → Status shows 🔊; Mute in the
   task manager → 🔇. Press **Use camera and microphone** on the test page
   and allow it. → 📷 and 🎙. *(status)*
10. Tick the Network column. Open a large download in a page (or the test
    page's **Fetch a large file** with `?file=` pointing to a large file
    the site allows). → The tab shows MB/s while it loads; an idle tab
    shows 0 B/s. *(network)*

## Actions

11. Select the busy tab → **End process**. → That tab shows "Aw, Snap!"
    with Reload; other tabs keep working. Reload brings the page back.
    *(end process)*
12. Open two tabs that share a process (step 5) and End process on one. →
    Brouk asks first and names the other tab. *(shared process asks)*
13. Select a test tab (counter running) → **Pause**. → The counter stops,
    CPU drops to about 0 %. Switch to the tab. → It stays paused: pause
    icon and dimmed title in the tab strip, "This page is paused" bar with
    Resume. Click **Resume**. → The counter continues from the same value.
    *(pause; stays paused when shown)*
14. Right-click a tab → Pause Tab / Resume Tab. → The same.
15. Select a tab that is not shown in its window → **Unload tab**. → Its
    process and values disappear, it says "not loaded"; the tab keeps its
    title. Switch to it. → The page loads again. Unload is disabled for the
    tab shown in its window. *(unload)*
16. Double-click a row of a tab in the other window, minimised or on
    another Space. → That window comes forward (switching Space/display)
    with the tab selected. *(go to tab)*
17. Reload and DevTools buttons act on the selected tab.

## Table and copy

18. Sort by a few columns, reverse; filter by part of a title, an address
    and a profile name; Pause updates (values stop changing); change the
    columns. Close and reopen the window. → Columns and sort order are
    kept. *(sorting, filter, pause updates, columns remembered)*
19. Select two rows → Copy CSV, paste into a spreadsheet; Copy JSON, paste
    into a JSON viewer. → Valid, with the visible columns and the time;
    JSON has the graph samples of those rows. *(copy)*
20. Select one row, then ⌘-click a second. → The graph shows a solid (CPU)
    and dashed (memory) line per row, updating; pointing at it shows
    values and time. Settings → Task manager → Graph history 1 minute →
    the graph covers 1 minute. *(graph)*
21. Collapse the graph, reopen the window. → It stays collapsed.

## Shortcuts, idle cost, accessibility

22. ⇧⌘⎋ opens the task manager. Settings → Shortcuts → Task manager:
    change it to e.g. ⌃⌘T. → The new one works, ⇧⌘⎋ doesn't.
    *(shortcut)*
23. With the task manager closed for a minute, Activity Monitor shows no
    CPU use by Brouk for it (no polling). *(closed: no polling)*
24. Use only the keyboard: Tab into the table, arrows, Shift+arrows,
    Return, P, U, M. With VoiceOver: rows read title and status words
    ("paused", "playing audio"); values changing don't make it re-read the
    table. *(keyboard; VoiceOver)*
25. The "?" next to the title opens help at Task manager. *(help)*
