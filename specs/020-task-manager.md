# 020 – Task manager

- **Status:** ready
- **Priority:** P2
- **Depends on:** 003, 005, 012 (audio indicator); works with 006 (DevTools), 015 (tabs that load when shown), 016 (help)

## Goal

See **which page is using the CPU, memory or GPU**, and deal with it without hunting through windows. Chromium starts many helper processes. macOS Activity Monitor lists them all as "Brouk Helper (Renderer)" and gives no way to tell which tab one belongs to. Activity Monitor can't be made to show page titles (see Notes), so Brouk shows this information itself, like Chrome's Task Manager, with the extra details that developers and website testers need.

## Description

### Window

- The task manager opens in **its own window**, one for the whole app (not one per browser window). Opening it again brings the existing window to the front.
- It opens from **Window → Task Manager**, from the tab's right-click menu (**Show in Task Manager**, which also selects that tab's row) and with a shortcut (`shortcuts.task_manager`, default **Shift+Cmd+Esc**, close to Chrome's Shift+Esc on Windows and Linux; remappable like every shortcut, 003).
- It is an internal HTML page, `brouk://task-manager`, shown in that window (like help, 016). It can't be opened in a normal tab.
- The window remembers its size and position. While it is closed, Brouk collects no task data, so it costs nothing when nobody is looking.

### What is listed

- **One row per task**, as in Chrome: every tab (all windows, all profiles), the browser process, the GPU process, utility processes (network, storage, audio…), Brouk's own pages (settings, history…, marked as Brouk pages) and the workers a page runs (dedicated, shared and service workers).
- **Rows that share a process are grouped**, so it is clear when several tabs live in one renderer and ending that process affects all of them. Workers and subframe processes appear under the tab they belong to.
- Tabs that haven't loaded yet (015) or have been unloaded (below) are listed as **Not loaded**, with no process and no resource values.

### Columns

Shown by default:

| Column | Content |
|---|---|
| Task | Favicon and page title (the address if there's no title); the task type for other tasks ("GPU process", "Service worker: example.com") |
| Profile | The tab's profile, in the profile's colour (005) |
| Memory | The process's memory footprint (the same value Activity Monitor shows as "Memory") |
| CPU | CPU use of the process in %, where 100 % is one full core (as in Activity Monitor) |
| Status | Icons: playing audio, muted, using camera, microphone or screen sharing, paused, loading, crashed, DevTools open |
| Process ID | The operating system's process ID, to match the row with Activity Monitor or `top`. Reported by each renderer itself; rows of processes that report none (GPU, utility) stay empty unless CEF provides the ID |

Available from the column picker (right-click the header):

| Column | Content |
|---|---|
| GPU memory | GPU memory of the task (the GPU process's value includes everything it serves, marked as such) |
| JavaScript memory | JavaScript heap used / allocated by the page |
| Network | Data received per second by the tab, counted by Brouk from the tab's responses. An approximation: responses served from the cache or by a service worker may be missing, so the header's tooltip says "approximate" |
| Address | The page's current address |
| Type | Tab, Brouk page, worker, GPU, utility, browser |
| Shared with | Number of other tabs in the same process |
| Window | The window the tab is in |

- Clicking a column header sorts by it. Clicking again reverses the order. Sorting keeps process groups together, ordered by the group's total.
- A **total row** at the bottom shows Brouk's total memory and CPU across all processes.
- Values refresh **every second**. **Pause updates** freezes the table, so a tester can read or copy values that keep changing.
- A **filter field** narrows the rows by title, address or profile.

### Actions

Actions apply to the selected row(s). Several rows can be selected (Cmd/Shift-click). Each action is a button, in the row's right-click menu and on a keyboard shortcut inside the window.

- **Go to tab** (double-click or Enter): brings the tab's window to the front, on its own Space and display, and selects the tab. For a worker or subframe row it goes to the tab that owns it.
- **End process**: stops the process right away. Every tab in that process shows Chromium's "crashed" page with a Reload button. If the process holds more than the selected tab, Brouk first asks, listing the tabs that will be affected. The browser process can't be ended, and neither can the GPU process (Chromium restarts it, so ending it achieves nothing).
- **Pause / Resume**: freezes the page. Its scripts, timers and media stop, and it uses no CPU, but it stays loaded and keeps its state.
  - A paused tab **stays paused** when the user switches to it (unlike Chrome, which resumes a frozen tab when it is shown), so testers can inspect a page in a frozen state. It resumes only with Resume.
  - Being paused must be obvious everywhere: the tab in the tab strip shows a pause icon in place of its favicon and a dimmed title; the tooltip says "Paused"; when the tab is shown, a bar at the top of the page says "This page is paused" with a **Resume** button; the task manager's Status column shows the pause icon. VoiceOver reads "paused" with the tab's title.
  - The tab's right-click menu has Pause / Resume too.
  - Paused state is not kept across a restart: restored tabs (015) start unpaused.
- **Unload tab**: frees the tab's memory but keeps the tab in the tab strip, with its title. The tab loads again when the user switches to it (like tabs at startup, 015). Form contents and page state are lost, as when Chrome discards a tab.
- **Mute / Unmute** the tab.
- **Reload** the tab.
- **Open DevTools** for the tab (006).
- **Copy**: copies the selected rows, or the whole table, as CSV or JSON, with the time of the copy, for bug reports.

### History graph

- Below the table, a **graph pane** shows CPU and memory of the selected row over the last **5 minutes** (configurable), one sample per refresh. CPU and memory are two lines with their own scales; hovering shows the exact value and time.
- With several rows selected, each gets its own line, in a distinct colour, so tabs can be compared. Process groups are graphed as the process total.
- Brouk keeps this history for every task **only while the task manager window is open**; the graph starts empty when the window is opened. A task's history disappears when the task ends.
- Pause updates also pauses the graph. The graph's data is included in Copy as JSON (per row, a list of time, CPU, memory).
- The pane can be collapsed; its state is remembered.

### Accessibility and keyboard

- The table is navigable with the arrow keys. Every icon has a text equivalent for VoiceOver, and every button has a label.
- Changes in values don't make VoiceOver re-read the table.

## Acceptance criteria

- [ ] Window → Task Manager opens the task manager window; choosing it again brings the same window to the front.
- [ ] Show in Task Manager in a tab's right-click menu opens the window with that tab's row selected.
- [ ] Every open tab of every window and profile has a row with its title and profile; the browser, GPU and utility processes have rows of their own.
- [ ] Tabs that share a process are shown as one group.
- [ ] A tab running a busy loop (`while(true){}`) shows CPU near 100 % within 3 seconds and sorts to the top when sorted by CPU.
- [ ] A tab allocating a large array shows growing Memory and JavaScript memory.
- [ ] The Process ID column matches the process ID shown for the same renderer in Activity Monitor.
- [ ] The Status column shows audio for a tab playing a video with sound, muted after Mute, and camera/microphone for a tab using `getUserMedia`.
- [ ] Go to tab brings the right window to the front and selects the tab, including a window on another Space or display.
- [ ] End process on a tab that has its own process shows the crashed page in that tab only; Reload brings the page back.
- [ ] End process on a process shared by several tabs asks first and lists those tabs.
- [ ] Pause on a tab with a running counter stops the counter and drops its CPU to about 0 %; Resume continues it from the same value.
- [ ] Unload tab removes the tab's process and memory from the list; switching to the tab loads the page again.
- [ ] Not-loaded tabs are listed as Not loaded, without a process.
- [ ] Sorting, filtering, pausing updates and the column picker work; the chosen columns and sort order are remembered.
- [ ] Copy produces valid CSV and JSON containing the visible columns.
- [ ] Shift+Cmd+Esc opens the task manager; after remapping `shortcuts.task_manager`, the new shortcut works and the old one doesn't.
- [ ] A paused tab stays paused after switching to it; its tab shows the pause icon and dimmed title, the page shows the "This page is paused" bar, and its Resume button resumes the page.
- [ ] The Network column shows data per second for a tab downloading a large file and about 0 for an idle tab.
- [ ] Selecting a row shows its CPU and memory graph, updating every refresh; selecting two rows shows two lines; the graph covers at most the configured history length.
- [ ] Copy as JSON includes the graph samples of the copied rows.
- [ ] With the task manager closed, Brouk does not poll task data.
- [ ] All controls have VoiceOver labels and the table works with the keyboard alone.
- [ ] (after 016) The task manager has a help section and a "?" link.

## Out of scope

- Renaming processes in macOS Activity Monitor (not possible, see Notes).
- Limiting a tab's CPU or network speed. That is emulation for testing and belongs to DevTools (006).
- Keeping task history while the task manager is closed, or saving it to disk.
- Automatically pausing or unloading tabs in the background (Chrome's Memory Saver). That could be a later spec.
- Extensions: Brouk has none.

## Configuration

| Setting | Default | Scope |
|---|---|---|
| `shortcuts.task_manager` | `shift+cmd+escape` | global |
| `task_manager.refresh_interval` | 1 s (0.5–10 s) | global |
| `task_manager.columns` | Task, Profile, Memory, CPU, Status, Process ID | global, also changed from the column picker |
| `task_manager.sort` | Memory, descending | global, also changed by clicking headers |
| `task_manager.history_length` | 5 minutes (1–60 minutes) | global |

## Open questions

None.

## Notes

- 2026-10-25 (research): macOS Activity Monitor takes a helper's name from its app bundle (`Brouk Helper (Renderer).app`). Changing `argv[0]`/`setprogname` affects only `ps` and `top`. The private LaunchServices call that sets a display name can't be used from sandboxed renderers, and helpers aren't registered with LaunchServices, so the browser can't use it for them either. Chromium also puts several tabs into one renderer, so one title per process would often be wrong. Chrome's answer is its own Task Manager, which this spec follows.
- 2026-10-25 (CEF headers checked, `include/cef_task_manager.h`, `internal/cef_types.h`): `CefTaskManager` (UI thread only) provides `GetTaskIdsList` (rows that share a process are next to each other, in process-tree order), `GetTaskInfo` (`id`, `type`, `is_killable`, `title`, `cpu_usage` (0 to processors × 100 %), `number_of_processors`, `memory`, `gpu_memory`, `is_gpu_memory_inflated`), `KillTask` and `GetTaskIdForBrowserId`. Task types: browser, GPU, utility, renderer, extension, guest, sandbox helper, dedicated/shared/service worker. It reports no process ID and no network use, so Brouk gets both itself (decisions 2 and 3 below).
- Other CEF pieces for this spec: `CefBrowserHost::SetAudioMuted`/`IsAudioMuted` (mute); `CefDisplayHandler::OnMediaAccessChange` (camera/microphone/screen in use); the audio estimate from 012's page script (CEF reports no "playing audio" event); `CefBrowserHost::ExecuteDevToolsMethod` for pause/resume (`Page.setWebLifecycleState` frozen/active) and JavaScript memory (`Runtime.getHeapUsage`). These still need to be tried in a build.
- 2026-10-25 (user's decisions): (1) the shortcut gets a default, Shift+Cmd+Esc (Shift+Esc alone isn't allowed: shortcuts need Cmd or Ctrl, 003; Opt+Cmd+Esc is macOS Force Quit); (2) process IDs reported by the renderers themselves are acceptable, GPU and utility rows may stay empty; (3) the Network column is counted by Brouk, approximate; (4) a paused tab stays paused when shown, and must be clearly marked as paused; (5) the history graph is part of this spec.
