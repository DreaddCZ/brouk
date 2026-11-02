# Brouk's icon and logo

Every file here, the macOS `src/platform/mac/AppIcon.icns` and the Windows
`src/platform/win/app.ico` are written by `scripts/make_icons` from one
drawing of the beetle in `scripts/make_icons.mjs`. Change the drawing
there and run the script on a Mac (it needs Google Chrome and Xcode's
command line tools); don't edit the generated files by hand.

| File | What it is |
|---|---|
| `icon-macos.svg` | The app icon on Apple's grid (824 × 824 tile in a 1024 square) |
| `icon-windows.svg` | The app icon for Windows, the tile filling the square |
| `icon-windows-small.svg` | Windows at 32 px and less: a bigger beetle, thicker legs |
| `symbol.svg` | The beetle alone, in colour, on transparency |
| `symbol-black.svg`, `symbol-white.svg` | The beetle in one colour, for light and dark backgrounds |
| `icon-layer-background.svg` | The navy background, a layer for Apple's Icon Composer (macOS 26); the foreground layer is `symbol.svg` |
| `social-preview.png` | 1280 × 640, for GitHub → Settings → Social preview |

## Colours

| Name | Hex | Use |
|---|---|---|
| Mint | `#6EE7B7` | Top of the beetle's gradient |
| Teal (primary) | `#14B8A6` | The beetle |
| Blue (accent) | `#3B82F6` | Bottom of the beetle's gradient |
| Navy | `#0B1220` | The tile, dark backgrounds |
| Navy, light | `#1B2A47` | Top of the tile's gradient |
| Light grey | `#E5E7EB` | Light backgrounds |
