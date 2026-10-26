## Welcome to Brouk {#welcome}

Brouk is a web browser for work. It keeps several **profiles** side by side in one window, so you can be signed in to several accounts of the same service at once, and it opens every link in the right profile.

- New here? Read [Profiles](#profiles) and [Link routing](#link-routing) first.
- Look for the small **?** buttons in Brouk's own pages: each opens this help at the part that explains them.
- Search this help with the box at the top left. It ignores case and accents.
- **Help → Brouk Help** (<kbd>⇧⌘?</kbd>) opens this window; **Help → Keyboard Shortcuts** lists every shortcut.
- The command palette (<kbd>⌘K</kbd>) finds help sections too: type "help" and a topic.

### Installing {#installing}

1. Download the disk image for your Mac from the [releases page](https://github.com/DreaddCZ/brouk-releases/releases): `arm64` for Apple Silicon (M1 and later), `x86_64` for Intel. Apple menu → About This Mac shows which one you have.
2. Open the disk image and drag **Brouk** onto **Applications**.
3. Open Brouk from Applications. It is signed and notarized by Apple, so it opens without a warning.

To make Brouk your default browser, see [Default browser](#link-routing-default-browser).

### Updates {#updates}

Brouk updates itself, like Chrome:

- It checks for a new version about a minute after it starts and then every few hours.
- A new version is downloaded in the background and installed the next time you quit Brouk. Your windows and tabs come back as after any restart.
- Once an update is downloaded, **Brouk → Restart to Update** installs it at once.
- **Brouk → Check for Updates…** checks at any time and shows what is new.

Settings → [Updates](#settings-updates):

- **Update channel:** *Stable*, or *Beta* to get test versions early. Going back from Beta to Stable never installs an older version; you stay on yours until a newer stable one is out.
- **Updates:** install automatically (default), ask before downloading, or don't check by itself.

Brouk checks only by downloading the update feed; it sends nothing but its version. Copies you build yourself never update. **Brouk → About Brouk** shows the version and the exact build, e.g. `0.1.0 (a1b2c3d)` — mention it when you report a problem.
