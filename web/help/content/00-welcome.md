## Welcome to Brouk {#welcome}

Brouk is a web browser for work. It keeps several **profiles** side by side in one window, so you can be signed in to several accounts of the same service at once, and it opens every link in the right profile.

- New here? Read [Profiles](#profiles) and [Link routing](#link-routing) first.
- Look for the small **?** buttons in Brouk's own pages: each opens this help at the part that explains them.
- Search this help with the box at the top left. It ignores case and accents.
- {mac}**Help → Brouk Help**{/mac}{win}**⋮ → Brouk Help**{/win} ({shortcut:help}) opens this window; {mac}**Help → Keyboard Shortcuts**{/mac}{win}[Keyboard shortcuts](#keyboard-shortcuts){/win} lists every shortcut.
- The command palette ({shortcut:command_palette}) finds help sections too: type "help" and a topic.

### Installing {#installing}

{mac}
1. Download the disk image for your Mac from the [releases page](https://github.com/DreaddCZ/brouk-releases/releases): `arm64` for Apple Silicon (M1 and later), `x86_64` for Intel. Apple menu → About This Mac shows which one you have.
2. Open the disk image and drag **Brouk** onto **Applications**.
3. Open Brouk from Applications. It is signed and notarized by Apple, so it opens without a warning.
{/mac}
{win}
Download `Brouk-<version>-setup.exe` from the [releases page](https://github.com/DreaddCZ/brouk-releases/releases) and run it. It installs Brouk for you only (in `%LOCALAPPDATA%\Programs\Brouk`), so it needs no administrator rights, and adds it to the Start menu. The installer is not code-signed: Windows SmartScreen warns about an unknown publisher; choose **More info → Run anyway**. Uninstall it in Windows Settings → Apps; your profiles, passwords and settings stay in `%LOCALAPPDATA%\Brouk`.
{/win}

To make Brouk your default browser, see [Default browser](#link-routing-default-browser).

### Updates {#updates}

Brouk updates itself, like Chrome:

- It checks for a new version about a minute after it starts and then every few hours.
- A new version is downloaded in the background and installed the next time you quit Brouk. Your windows and tabs come back as after any restart.
- Once an update is downloaded, {mac}**Brouk → Restart to Update**{/mac}{win}**⋮ → Restart to update Brouk**{/win} installs it at once.
- {mac}**Brouk → Check for Updates…**{/mac}{win}**⋮ → Check for updates…**{/win} checks at any time and shows what is new.

Settings → [Updates](#settings-updates):

- **Update channel:** *Stable*, or *Beta* to get test versions early. Going back from Beta to Stable never installs an older version; you stay on yours until a newer stable one is out.
- **Updates:** install automatically (default), ask before downloading, or don't check by itself.

{win}
With *ask before downloading*, **⋮ → Update Brouk to …** downloads it. The installer runs by itself after Brouk quits. Every update is checked against Brouk's signing key before it runs.

**Brouk from the Microsoft Store** is updated by the Store, not by Brouk: Settings → Updates says so, and **⋮ → Check for updates…** opens the Store's updates page. See [Microsoft Store](#microsoft-store).
{/win}

Brouk checks only by downloading the update feed; it sends nothing but its version. Copies you build yourself never update. {mac}**Brouk → About Brouk** shows the version and the exact build{/mac}{win}`brouk.exe --version` in PowerShell shows the version and the exact build (**⋮ → Check for updates…** shows the version){/win}, e.g. `0.1.0 (a1b2c3d)` — mention it when you report a problem.

### Microsoft Store {#microsoft-store}

{mac}
Brouk is in the Microsoft Store for Windows only. On a Mac, install it as described in [Installing](#installing).
{/mac}
{win}
Brouk from the Microsoft Store is a copy of its own, next to the direct download: installing it does not replace the direct download, and each copy has its own profiles, settings, history and passwords. The Store app keeps its data in its own folder under `%LOCALAPPDATA%\Packages` (Settings → Settings files → **Show in File Explorer** opens it). Windows deletes that folder when you uninstall the Store app, so [export](#profiles-export) the profiles you want to keep first.

The Store updates it (see [Updates](#updates)). To make it the default browser, Settings → Links opens Windows' default apps page for it.

When the direct download has data on this computer, the Store app asks at its first start whether to bring it in:

- **Move** — the Store app gets the direct download's profiles, settings, history, bookmarks and passwords, and the direct download's data is deleted. Windows' Installed apps then opens, so you can uninstall the direct download.
- **Copy** — the Store app gets the same, and the direct download keeps its own. From then on each copy has its own data.
- **Not now** — the Store app starts empty and doesn't ask again.

Settings → Advanced → **Import from the direct download…** does the same at any time. It replaces the Store app's data. Brouk restarts to bring the data in; if the direct download is running, Brouk asks you to quit it first. Caches and logs are not copied, and a Bitwarden vault asks to be unlocked again.
{/win}
