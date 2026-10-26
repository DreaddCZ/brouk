# Manual test checklist: profiles (005)

Each step names the acceptance criterion it checks. Use a test copy or be
ready to delete the profiles you create here.

## Creating and editing

1. Profiles → New Profile…: name "Test A", colour green. → It appears in
   the Profiles menu and in Profiles → Manage Profiles…. No sign-in is asked
   for, now or later. *(create, name and colour; no online account)*
2. In the profile manager rename "Test A" to "Test B" and change its colour.
   → The tab group label and colour change in every window at once.
   *(rename, colour)*
3. Try to create a second profile called "test b". → Refused: names are
   unique, ignoring case.

## Isolation

4. Open a tab of Main and one of Test B in the same window. Sign in to the
   same site (e.g. github.com) with different accounts in each. → Each tab
   stays signed in to its own account after reloading. *(logins separate,
   also in one window)*
5. Visit a page in Test B only, then type its address in a Main tab. → Not
   suggested in Main; ⌘Y lists it only under Test B. *(history separate)*
6. Save a password in Test B. → Main does not offer it. *(passwords
   separate)*
7. Change a profile setting in Test B (e.g. its download folder). → Main is
   unchanged. *(settings separate)*
8. Save an address in the autofill of Main. → It is offered in Test B too.
   *(autofill shared)*
9. Bookmark a page linked to Test B and another one as General. → In a Main
   tab only the general one shows in the bar, menu and suggestions; in a
   Test B tab both do. *(linked bookmarks)*
10. With tabs of Main and Test B in one window, switch between them. → The
    bookmarks bar and password filling follow the active tab's profile.

## Windows and groups

11. Open Test B tabs in two windows, and Main tabs in both. → Each window
    shows one group per profile, with the profile's name and colour; all of
    a profile's tabs in a window are in its one group. *(several profiles at
    once; one group per profile)*
12. ⌘T and ⌘N. → Open in the default profile. The "+" of Test B's group
    opens a new tab in Test B. *(default profile; group "+")*
13. Drag a tab from Main's group into Test B's group. → A dialog asks to
    continue or cancel. Cancel → nothing changes. Again with Continue → the
    page reloads in Test B (signed out where only Main was signed in).
    *(moving a tab)*

## Starting

14. Quit Brouk. In Terminal: `open -a Brouk --args --profile "Test B"`
    (or run the app's binary with `--profile "Test B"`). → It starts with a
    Test B tab. *(command line)*
15. Profile manager → turn on "Show this window when Brouk starts", quit and
    start. → The profile manager opens instead of a window; opening a
    profile closes it. Turn it off, quit and start → a browser window opens
    in the default profile. *(manager at startup)*

## Deleting

16. In the profile manager try to delete Main while it is the default. → Not
    possible. *(default cannot be deleted)*
17. With Test B tabs open and a bookmark linked to it, delete Test B. → The
    confirmation says how many tabs close and how many bookmarks become
    general. After confirming, its tabs close, the bookmark shows in every
    profile, and after the next start its data folders
    (`~/Library/Application Support/Brouk/profiles/<id>` and `web/<id>`)
    are gone. *(deleting removes data)*

## Export and import

18. Create "Test C" with a colour, a changed setting, a linked bookmark (in
    a folder) and a saved password. Profile manager → Export…: enter an
    export password. → A `.brouk-profile` file is saved. *(export)*
19. Open the file in a text editor. → It contains name, colour, settings and
    bookmarks, the passwords only as encrypted text, and no cookies,
    sessions or cache. *(contents; passwords encrypted; no cookies)*
20. Delete Test C, then Import… the file: give a wrong export password
    first. → "The export password is wrong." and it asks again. With the
    right one → the profile comes back with its name, colour, settings,
    bookmark (in its folder, linked to it) and password; sites need signing
    in again. *(import; password required)*
21. Import it once more and choose to go on without the passwords. → The
    profile is imported with no saved passwords.
22. The profile from step 21 is called "Test C 2" (names stay unique).

History in the export is spec 022, not tested here.
