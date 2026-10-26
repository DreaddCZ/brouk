## Profiles {#profiles}

A **profile** is a completely separate set of browser data: cookies and sign-ins, history, saved passwords, settings and cache. Use one profile per client, employer or account — for example one per Microsoft tenant, or one for work and one for personal browsing.

Profiles are local: there is no account to sign in to. Two things are deliberately shared: [autofill](#autofill) details, and bookmarks that are not linked to a profile ([Bookmarks and profiles](#bookmarks-profiles)).

### Profiles side by side {#profiles-groups}

- A window can hold tabs of several profiles. All tabs of one profile in a window form **one group**, labelled with the profile's name and colour.
- Click a group's label to collapse or expand it. The **+** after a group opens a new tab in that profile.
- New tabs and windows (<kbd>⌘T</kbd>, <kbd>⌘N</kbd>) open in the **default profile**. Its group is in every window, even without tabs.
- Tabs opened from a page (links, popups, "Open Link in New Tab") stay in the page's profile — unless a [link routing rule](#link-routing) sends them elsewhere.
- **Moving a tab to another profile:** drag it into the other profile's group. Brouk asks first, because the page reloads in the other profile and is signed in only if you are signed in there.
- The bookmarks bar, password filling and the new tab page follow the active tab's profile.

### Profile manager {#profiles-manager}

**Profiles → Manage Profiles…** opens the profile manager. There you can:

- create a profile (name and colour), rename it and change its colour;
- choose the **default profile**;
- list the **links that open in the profile** (one URL pattern per line, see [URL patterns](#url-patterns)); they are the same rules as in Settings → Links;
- open a profile in a new window;
- delete a profile. Its tabs close and all its data is removed after you confirm; its linked bookmarks become general. The default profile can't be deleted — make another profile the default first.

"Show this window when Brouk starts" makes the manager appear at startup, so you choose a profile first.

The **Profiles** menu opens a new tab in any profile; hold <kbd>⌥</kbd> for a new window.

### Export and import {#profiles-export}

**Export…** in the profile manager saves a profile to one `.brouk-profile` file, to move it to another Mac:

- included: name, colour, settings, the bookmarks linked to it, and its saved passwords;
- passwords are encrypted with an **export password** you choose; the file can't be imported without it (or import it without the passwords);
- not included: cookies, sign-ins and cache — sign in to sites again after importing.

**Import…** adds the profile as a new one. If its name is taken, " 2" is added.

### Starting a profile directly {#profiles-command-line}

Start Brouk with a profile from Terminal or a script:

```
open -a Brouk --args --profile "Work"
```

The profile's name or id works, also while Brouk is running. An unknown name opens the default profile.
