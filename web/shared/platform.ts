/**
 * The platform a page runs on. The internal pages are the same on macOS and
 * Windows (024); only keys and a few words differ.
 */

/** True on Windows. `platform` is navigator.platform ("MacIntel", "Win32"). */
export function onWindows(platform: string = typeof navigator === 'undefined' ? '' : navigator.platform): boolean {
  return platform.startsWith('Win');
}

/**
 * The key macOS writes ⌘ and Windows Ctrl, for shortcuts (⌘A / Ctrl+A) and
 * for ⌘-click / Ctrl-click.
 */
export function commandKey(event: { readonly metaKey: boolean; readonly ctrlKey: boolean }, windows = onWindows()): boolean {
  return windows ? event.ctrlKey : event.metaKey;
}
