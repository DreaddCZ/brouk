# Manual test checklist: password manager (008), built-in source

Serve the test pages first (`docs/testing/pages/008/README.md`):

    cd docs/testing/pages/008 && python3 -m http.server 8765

Brouk treats `localhost` like an https site. For the http checks use your
Mac's network address instead (`ipconfig getifaddr en0`, then
`http://<address>:8765/…`).

After a local rebuild macOS may ask once whether Brouk may use its key in
the keychain ("Brouk Keys"): choose **Always Allow**.

## Saving and filling

1. Open `http://localhost:8765/login.html`, type an email and a password,
   click **Sign in**. → On "Signed in" the key icon in the address bar turns
   blue and the prompt "Save password?" opens under it with the email and
   password (the eye shows the password). Click **Save**. → The key icon
   turns grey (logins exist for the site).
2. Go back to `login.html`. → Email and password are filled when the page
   loads.
3. Click into the email field. → A drop-down lists the login and "Manage
   passwords…". Up/Down move through it, Return fills, Escape closes it.
4. Sign in again with the same email and a **different** password. → The
   prompt says "Update password?"; **Update** saves the new password.
5. On `login.html` sign in with a new email, choose **Never for this site**.
   → Signing in again offers nothing; the site is listed under
   Passwords → Never saved, where **Allow saving** removes it.
6. Save a second login for the site. Reload `login.html`. → Nothing is
   filled automatically (two logins); the drop-down offers both.
7. Settings → Passwords → **Fill passwords only on click** on. Remove one of
   the logins on the passwords page and reload. → Nothing is filled until
   you choose the login in the drop-down. Turn the setting off again.

## Other hosts, frames, http

8. Open the page with your network address instead of localhost
   (`http://<address>:8765/login.html`). → Nothing is filled and nothing is
   offered (another site). Save a login there. → The drop-down starts with
   "This connection is not secure"; nothing is ever filled without a click.
9. Open `http://localhost:8765/frame.html`. → The login form inside the
   frame (from 127.0.0.1) is not filled with the localhost login.
10. On a real site with a login on another host of the same site (e.g. a
    login saved for `accounts.example.com`, page `www.example.com`), the
    drop-down offers it labelled with its host; it is never filled
    automatically.

## Multi-step login and generator

11. Open `http://localhost:8765/step1.html`. → The email is filled. Click
    **Next**. → On step 2 the password is filled.
12. Delete the saved logins, then sign in through step 1 and 2 by hand. →
    The prompt after step 2 shows the email from step 1.
13. Open `http://localhost:8765/signup.html` and click into the password
    field. → The drop-down offers "Use a strong password" with a 20
    character password. Choose it. → Both password fields are filled, and
    the passwords page already lists the login (saved at once). Type an
    email and click **Create**. → No prompt; the saved login now has the
    email as its user name.

## 2FA codes

14. On the passwords page edit the localhost login and paste
    `JBSWY3DPEHPK3PXP` as 2FA secret; save. Open `otp.html` and click the
    code field. → The drop-down offers "2FA code for …" with the code and
    the seconds left; choosing it fills the field. (Compare with any
    authenticator app using the same secret.)
15. Remove the 2FA secret again. Open `setup.html` (shows a QR code), click
    the key icon → **Read 2FA QR code from this page**. → "2FA code added";
    step 14 works again.
16. Settings → Passwords → **Copy the 2FA code after filling a login** on.
    Fill the login on `login.html` from the drop-down. → The current code is
    on the clipboard.

## Security key

17. On a site that supports security keys or passkeys (e.g.
    https://webauthn.io), register and sign in with a USB security key. →
    Works (handled by Chromium; Brouk has no passkey store of its own).

## Passwords page (⌥⌘P, Brouk → Passwords, "Manage passwords…")

18. ⌥⌘P opens the page as a tab, filtered to the tab's profile. Profile
    filter "All profiles" shows every built-in profile's logins with their
    colour; search by site and user name works.
19. **Show**, **Copy password** and **Edit…** ask for Touch ID (or the Mac's
    password) once; for the next 5 minutes they do not ask again. **Lock**
    locks at once. **Copy user** never asks.
20. **Delete** a login, then **Undo** in the notice. → It is back with its
    history.
21. **Add password**: site, user name, password, note, 2FA secret. → Listed;
    an invalid site or 2FA secret shows a clear message.
22. A second profile has its own logins: saving in one profile never shows
    in the other profile's drop-down.
23. A profile set to Bitwarden (profile settings) shows as one row "Uses
    Bitwarden — manage in Bitwarden". (Bitwarden itself follows in its own
    pull request.)

## Import, export, checkup

24. **Import…**: export passwords from Chrome (Settings → Passwords →
    Export), Firefox, Safari or Bitwarden as CSV and import into a chosen
    profile. → A summary lists added, skipped (exact duplicates) and
    conflicting entries; importing the same file again adds nothing.
25. **Export…** warns that the file is not encrypted, asks for Touch ID, and
    saves a CSV in Chrome's format.
26. **Checkup** → **Check passwords** (asks for Touch ID). → Lists breached
    passwords (save one login with the password `password123` to see one),
    reused passwords (the same password in two entries, also across
    profiles, each with its profile colour) and weak passwords.
27. Settings → Passwords → **Check saved passwords for breaches** off: the
    checkup says the breach check is off. Saving `password123` from a login
    prompt with the check on shows "This password was found in a data
    breach".

## Storage and profile export

28. `~/Library/Application Support/Brouk/profiles/<id>/passwords.db`: open
    it with any text viewer or `sqlite3`. → Sites and user names are
    readable, passwords, notes and 2FA secrets are not.
29. Profile manager → export a profile with passwords. → Brouk asks for an
    export password (twice). Import the file: → it asks for the password; a
    wrong one says so and asks again; with the right one the new profile
    has the passwords. "Import Without Passwords" imports the rest.
