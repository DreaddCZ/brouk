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

**Brouk from the Microsoft Store** is updated by the Store, not by Brouk: Settings → Updates says so, and **⋮ → Check for updates…** opens the Store's updates page. Its data is in `%LOCALAPPDATA%\Brouk` too, so it shares profiles, passwords and settings with the downloaded copy. If both are installed, the Store app offers once to uninstall the downloaded copy; your data stays either way. To make it the default browser, Settings → Links opens Windows' default apps page for it.
{/win}

Brouk checks only by downloading the update feed; it sends nothing but its version. Copies you build yourself never update. {mac}**Brouk → About Brouk** shows the version and the exact build{/mac}{win}`brouk.exe --version` in PowerShell shows the version and the exact build (**⋮ → Check for updates…** shows the version){/win}, e.g. `0.1.0 (a1b2c3d)` — mention it when you report a problem.
