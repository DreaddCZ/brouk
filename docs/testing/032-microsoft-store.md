# 032 – Microsoft Store app: manual test checklist

Run on Windows 10 or 11 x64, with the Store app installed (from the Store,
or the CI package installed as `scripts/test_windows_msix.ps1` does). "Direct
download" is Brouk installed with `Brouk-<version>-setup.exe`.

## Own data folder

1. Without the direct download's data (`%LOCALAPPDATA%\Brouk` absent), start
   the Store app. Expected: no question about the direct download;
   Settings → Settings files → Show in File Explorer opens
   `%LOCALAPPDATA%\Packages\<package family>\LocalState\Brouk`;
   `%LOCALAPPDATA%\Brouk` is not created.
2. Settings → Updates. Expected: "Brouk from the Microsoft Store is updated by
   the Store." ⋮ → Check for updates… opens the Store's updates page.

## Import at the first start

3. Install the direct download, add a profile, a bookmark and a saved
   password in it, quit it. Uninstall the Store app (its data goes), install
   it again and start it. Expected, about 2 s after the window opens: "Brouk
   from the direct download has data on this computer…" with Move, Copy,
   Not now.
4. Choose Copy. Expected: Brouk quits, a small "Bringing in the data…"
   window shows briefly, Brouk starts again with the direct download's
   windows, profiles, bookmark and saved password (the password fills in).
   The direct download still has all of it.
5. In the Store app, delete the profile you added; start the direct download.
   Expected: the profile and its saved password are still there (keys are
   separate).
6. Quit both. Settings → Advanced → Import from the direct download… in the
   Store app, while the direct download runs. Choose Move. Expected: Brouk
   quits and asks to quit the direct download first (Retry, Cancel). Quit the
   direct download, Retry. Expected: the data comes in,
   `%LOCALAPPDATA%\Brouk` is gone, and Windows' Installed apps opens.
7. Uninstall the Store app, install it again, start it with the direct
   download's data present, choose Not now. Expected: empty Store app; the
   question does not come at the next start; Settings → Advanced still
   offers the import.
