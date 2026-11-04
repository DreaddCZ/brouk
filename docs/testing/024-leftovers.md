# 024 – Windows leftovers: zoom indicator and site information

Run on Windows 11 x64.

1. Open a page, hold Ctrl and turn the mouse wheel up two notches. Expected: the page zooms and the address bar shows "110 %", then "125 %", right away (not only after switching tabs). Ctrl+wheel down back to 100 %: the indicator disappears.
2. Open `https://example.com`. Expected: a lock at the left of the address bar. Click it: a popup under it with "Connection is secure", `https://example.com`, **Certificate…** and "Permissions" ("This site has no permissions set."). Escape or clicking elsewhere closes it.
3. Click **Certificate…**. Expected: Windows' certificate dialog for the site, with its certification path.
4. Open `http://neverssl.com`. Expected: a warning icon; the popup says the connection is not secure and warns about entering passwords.
5. Open `https://expired.badssl.com` and proceed. Expected: a red warning icon; the popup tells the certificate is not valid.
6. On a site that asked for the camera or notifications (e.g. allow notifications on a test page), open the popup. Expected: the permission is listed with Allow; change it to Block. Expected: "Reload to apply the changes." and **Reload**; Reload reloads the page. **Reset permissions** sets every permission back to Ask.
7. Open `brouk://settings`. Expected: the "i" icon; the popup shows no address and no permissions.
8. Press F11 (full screen). Expected: the toolbar with the icon is hidden; leaving full screen shows it again.
