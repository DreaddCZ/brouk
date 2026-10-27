# Manual test checklist: password manager (008), Bitwarden source

You need the Bitwarden CLI, signed in once in Terminal (`bw status` says
`"status":"locked"`), and a profile whose Settings → Passwords →
**Passwords** is set to **Bitwarden**. Use throwaway items for saving and
updating: they really change your vault.

Serve the test pages (`docs/testing/pages/008/README.md`):

    cd docs/testing/pages/008 && python3 -m http.server 8765

## Setup steps (CLI missing or signed out)

1. Settings → Passwords → **Bitwarden CLI**: set `/nonexistent/bw`. Open
   `http://localhost:8765/login.html` in the Bitwarden profile. → The key
   icon shows a lock. Click it. → "Set up Bitwarden" explains how to install
   the CLI and sign in. Clear the setting again.
2. (Optional, only if you can sign in again afterwards) `bw logout` in
   Terminal, click the lock icon. → The steps say to sign in, with the
   `bw login` command; for a company server with its own certificates the
   command starts with `NODE_EXTRA_CA_CERTS=…`, pointing to
   `~/Library/Application Support/Brouk/bitwarden-certificates.pem`. Sign in
   again with that command.

## Unlocking

3. Quit and restart Brouk. Open `login.html` in the Bitwarden profile. → The
   key icon shows a lock; nothing is filled.
4. Click into the email field. → The drop-down offers "Unlock Bitwarden…".
   Choose it. → "Unlock Bitwarden" asks for the master password of your
   account on your server. Enter a wrong one. → It asks again, saying the
   password is not correct. Enter the right one. → The lock goes away.
5. Lock from the passwords page (⌥⌘P): the Bitwarden profile's row says
   "unlocked"; click **Lock Bitwarden**. → The row says "locked"; on
   `login.html` the key icon shows the lock again.
6. Click the lock icon. → Touch ID is asked ("unlock Bitwarden"); after it
   the vault is unlocked, no master password needed.
7. Quit and restart Brouk, click the lock icon. → Touch ID again unlocks it
   (the session survives a restart).
8. Lock the screen (⌃⌘Q) and log back in, or put the Mac to sleep. → The
   vault is locked again.
9. Settings → Passwords → **Unlock Bitwarden with Touch ID** off. Lock and
   unlock. → The master password is asked, no Touch ID. Turn it on again.
10. Settings → Passwords → **Lock Bitwarden after** 1 minute. Unlock, wait
    two minutes without filling anything, then click a login field. → It
    offers "Unlock Bitwarden…". Set it back to 15.

## Filling, saving, updating

11. Unlocked, on `login.html` sign in with a new email and password. → The
    prompt "Save password?" has **Save in** with "No folder", your folders
    and the organisations' collections you can add to. Choose a folder,
    **Save**. → The item appears in that folder in Bitwarden (web vault or
    app, after a sync there), named `localhost`.
12. Reload `login.html`. → Email and password are filled when the page
    loads.
13. Sign in with the same email and another password. → "Update password?";
    **Update** changes the item's password in Bitwarden.
14. Repeat 11 choosing an organisation's collection. → The item is in that
    collection.
15. A real site with an item in your vault (with its default match
    setting): open its login page. → The login is filled. On another host of
    the same site (e.g. `shop.` instead of `www.`) it is filled too, as in
    Bitwarden.
16. In Bitwarden, set an item's URI match to **Exact** with the full address
    of a page. → It is filled on that address only, not on other pages of
    the site. Set one to **Never**. → It is not offered.
17. On a new-password field (`http://localhost:8765/signup.html`), choose
    "Use a strong password". → A new item without a user name is saved at
    once; after the sign-up it gets the user name.

## Re-prompt and 2FA

18. In Bitwarden, turn on **Master password re-prompt** for the `localhost`
    item (sync Brouk: lock and unlock). Reload `login.html`. → Nothing is
    filled automatically. Choose the login in the drop-down. → Touch ID is
    asked first ("fill a protected Bitwarden login"); after it the login is
    filled. With Touch ID off in settings, the master password is asked
    instead.
19. An item with a TOTP key (if your plan or server has TOTP): on its site's
    code field (`http://localhost:8765/otp.html` for a `localhost` item
    with a TOTP key) the drop-down offers the current code with a countdown.

## Safety

20. While Bitwarden is unlocked, run `ps -Ewwo command | grep [b]w` in
    Terminal during a fill or save. → No password or session key appears in
    `bw`'s arguments. (Environment variables of your own processes are
    visible to you only.)
21. `ls ~/Library/Application\ Support/Brouk/` → apart from
    `bitwarden-certificates.pem` (public certificates of the server, if it
    uses its own), nothing about Bitwarden is written there.
