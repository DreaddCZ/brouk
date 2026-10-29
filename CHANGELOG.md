# Changelog

What changed in each release of Brouk. Written by `scripts/release` (spec 019).

## 0.2.1 – 2026-10-29

### New

- Open-source licences page: Help → Open-Source Licences opens a tab that lists Brouk's licence and every third-party component with its licence text, including Chromium's notices. About Brouk points to it.

### Changed

- Updated the browser engine to Chromium 156 (CEF 156.0.14).

### Fixed

- Fixed a rare crash when opening a link from another app.

## 0.2.0 (beta) – 2026-10-28

### New

- **Task manager**: a new window that lists open tabs and the browser's background tasks, grouped by process, with memory, CPU, network use and process IDs. From it you can pause or unload tabs to free up resources. A paused tab shows a still picture of the page until you resume it. Settings let you adjust the task manager, and help has a new section on it.

## 0.1.2 (beta) – 2026-10-27

### New

- Profiles can now use your Bitwarden vault for passwords, through the official Bitwarden command-line tool. Brouk finds, fills, saves and updates logins in the vault. New logins can go to a folder or collection you choose.
- While the vault is locked, the key icon shows a lock. To unlock, click the key icon or a login field, then use Touch ID or your master password. A spinner shows while the vault unlocks. If you type the wrong master password, the sheet stays open so you can try again.
- Logins marked "master password re-prompt" are never filled automatically. Brouk asks before filling them.
- The vault locks after a set idle time, when your Mac sleeps or the screen locks, and when you lock it from the passwords page.
- Bitwarden logins work the way they do in Bitwarden. A login saved for a site's base domain fills on every host of that site, whether the page uses http or https. On http pages, logins still fill only when you click a login field.
- Self-hosted Bitwarden servers that use certificates your Mac trusts work out of the box.

## 0.1.1 (beta) – 2026-10-27

### New

- Help: open the built-in help from the Help menu to read how Brouk's features work. Settings and other parts of the app have "?" buttons that open the matching help section.

### Fixed

- Automatic updates now find new releases. The previous release was published without its update information, so the browser couldn't see it.
- Bold text in release notes now appears correctly in the update window.

## 0.1.0 (beta) – 2026-10-26

### New

- **Profiles side by side:** each profile keeps its own cookies, cache and storage. Its tabs form one labelled, coloured group in the tab strip, and several profiles can share a window. A profile manager creates, renames, recolours, deletes, exports and imports profiles.
- **Link routing:** set Brouk as your default browser, and links from other apps open in the right profile based on your rules. When several profiles or windows fit, a picker with window thumbnails lets you choose. Links wrapped by common mail and chat security services are routed by their real address. You can edit the rules in Settings → Links, in Settings → Profiles or in the profile manager.
- **Tabs:** pinned tabs, tabs that shrink and collapse when space runs out, close other tabs or tabs to one side, duplicate a tab, reopen closed tabs (Shift+Cmd+T), and a speaker icon that mutes tabs playing sound.
- **Session restore:** the windows and tabs open when you quit come back at the next start. After a crash, you are offered to restore your pages.
- **Address bar suggestions** from your history and your search engine, with inline completion.
- **Command palette (Cmd+K)** that finds open tabs, browser actions and settings.
- **Settings page (Cmd+,):** settings are grouped by topic, with a tab for each profile. Changes apply right away, and settings can be exported and imported. Every keyboard shortcut can be changed.
- **History (Cmd+Y)** across all profiles, grouped by site or by day. You get powerful search filters, a read-only SQL query editor, a Clean up tool that keeps the sites you care about, and Clear browsing data. You can also list sites that are never recorded.
- **Bookmarks:** star button, bookmarks bar, Bookmarks menu, bookmark manager, and HTML import and export. Bookmarks can be linked to a profile and show site icons.
- **Downloads:** a toolbar button with a progress ring, plus a downloads page (Option+Cmd+L) across all profiles. You can pause, resume, retry, open, show in Finder and drag out downloads. Each profile can have its own download folder.
- **Password manager:** saves logins and fills them in, generates passwords and shows 2FA codes. It can read a site's 2FA QR code straight from the page. A passwords page (Option+Cmd+P) is protected by Touch ID and offers CSV import and export and a checkup for weak, reused and breached passwords. Profile exports can include passwords, protected by an export password.
- **Autofill** for contact, address and company details (including Czech and EU company and VAT IDs and IBANs). It avoids saving duplicates, and you can manage entries in Settings → Autofill.
- **Built-in ad blocking:** filter lists for each profile, your own filters, an element picker, a shield button that shows what was blocked, and optional Secure DNS.
- **Developer tools:** Chromium DevTools docked in the tab. Extra tools for requests (with Postman export), JWT tokens and site data. Rules can redirect, block or change headers, and JSON is shown as a searchable tree.
- **Site permissions** prompts like Chrome's, and a site info popover with connection security and certificate details.
- **Everyday browsing:** find in page, zoom remembered per site, printing, page full screen, Chrome-style context menus, and a pop-up blocker with an address bar indicator. Brouk asks before opening links in other apps, lets you choose which screen or window to share, and shows a crash page with reload.
- **Automatic updates** on a stable or beta channel.
