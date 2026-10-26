# 010 – Link routing: manual test checklist

Setup: profiles Main (default), Work and Client. In Settings → Links add the rules `companya.atlassian.net` → Work, `portal.azure.com` → Work, Client, and `github.com/company-a/*` → Client.

**Default browser**

1. Settings → Links shows "Make Brouk the default browser". Click it and confirm in the macOS dialog. Expected: the section now says Brouk is the default browser; System Settings → Desktop & Dock → Default web browser shows Brouk.

**Links from other apps**

2. In Notes (or Mail, Slack, Teams) click `https://companya.atlassian.net/browse/X-1`. Expected: Brouk comes to the front and the page opens in Work without a question.
3. Click `https://example.com/`. Expected: it opens in Main, in the most recently used window on the screen you clicked on.
4. Click `https://portal.azure.com/`. Expected: a picker centred on the screen with the URL (host in bold), "From Notes", and Work and Client numbered 1 and 2. Press `2`. Expected: it opens in Client.
5. Repeat step 4 and use ↓ and Return, then the mouse. Expected: both work. Press Esc in the picker. Expected: it closes and nothing opens.
6. Open a second window with a Work tab (Profiles menu, Option-click Work). Click the Jira link from step 2 again. Expected: the picker asks only for the window, offering both Work windows (most recent first) with a thumbnail of the active tab, its favicon and title, the window's groups with tab counts (Work in bold), and "New window" last. `3` opens it in a new window in Work.
7. Click the Azure link, choose Work, then in the window step press Esc or ←. Expected: back to the profile step. Press `A` (the "Always open…" box ticks), then `2`. Expected: it opens in Client and Settings → Links now lists only Client for `portal.azure.com`; the next Azure link opens in Client without asking.
8. Put a Work window on another Space (Mission Control) and click the Jira link. Expected: macOS switches to that Space. Set Settings → Links → "When the chosen window is on another Space" to "Move the window here" and repeat. Expected: the window comes to the current Space.

**Inside the browser**

9. In a Main tab, Cmd-click a link to `companya.atlassian.net`. Expected: a Work tab opens (in the Work window, which stays in the background; the Main tab stays in front). Cmd-Shift-click it. Expected: the Work window comes to the front.
10. In a Main tab, right-click a GitHub link to `github.com/company-a/…` → Open Link in New Tab. Expected: it opens in Client. A link to `github.com/other/…` stays in Main.
11. On a page with a `target=_blank` link to the Jira site (e.g. a Confluence page in Main), click it. Expected: it opens in Work.
12. In a Work tab, Cmd-click an Azure link. Expected: no picker; it opens next to the tab in Work (Work is one of the rule's profiles).
13. Open a new tab (⌘T, Main) and type `companya.atlassian.net/browse/X-1`, Return. Expected: it opens in Work and the empty new tab closes. Type the same into a tab already showing a page. Expected: it loads in that tab, in its profile.
14. Sign in somewhere that redirects through another domain (e.g. a Microsoft or GitHub sign-in in a Client tab) and a site whose sign-in opens a popup window. Expected: the redirect and the popup stay in the tab's profile, whatever the rules say.

**Link wrappers**

15. Send yourself a mail in Outlook with the Jira link (Safe Links rewrites it to `….safelinks.protection.outlook.com/?url=…`) and click it. Expected: it opens in Work without a question; the tab first shows the Safe Links address, then the Jira page.
16. The same for a link in Teams (Teams Safe Links page) and, if you get them, a Proofpoint `urldefense.com` link. Expected: as in step 15. Please send me a few of these real links (any target) so the decoders can be checked against them.
17. A Mimecast link (`protect-….mimecast.com` / `….mimecastprotect.com`). Expected: after a moment it opens in the profile for its real address, with no Mimecast tab. If Mimecast shows its own page (a warning), a small "Checking link · via Mimecast" window shows it; after "continue" the window closes and the link is routed. Closing that window cancels the link.
18. A wrapped link that matches the Azure rule. Expected: the picker shows the real Azure URL with "via Safe Links" (or Mimecast); "Always open…" changes the Azure rule, not one for the wrapper's host.
19. Settings → Links → Add wrapper: `links.example.com`, "In a parameter", `target`. Open `https://links.example.com/r?target=https%3A%2F%2Fcompanya.atlassian.net%2F` from Notes. Expected: routed to Work (the page itself fails to load, as the wrapper host does not exist).

**Settings → Links**

20. Add a rule with a pattern like `bad pattern`. Expected: it is marked as not valid and nothing is saved until it is fixed. Add a rule with only a pattern: it stays on the page and is saved once a profile is added.
21. Reorder a rule's profiles with ↑ and check that the picker offers them in the new order. Delete a rule; the next matching link is not routed by it.
22. Profiles → Manage Profiles → Edit on Work. Expected: "Links that open in this profile" lists `companya.atlassian.net` and `portal.azure.com` (with "Shared with Client"). Add `*.sharepoint.com`, remove the Azure line and Save. Expected: Settings → Links shows `*.sharepoint.com` → Work and `portal.azure.com` → Client only. Enter `bad pattern` and Save. Expected: the line is marked and nothing changes. Create a new profile with a link. Expected: the rule appears for it.
23. Edit `~/Library/Application Support/Brouk/link-routing.yaml` while Brouk runs (e.g. change a rule's profile). Expected: the next link follows the change. An invalid entry is listed at the top of Settings → Links.
