// Screenshots for the Microsoft Store listing (032), taken on a Windows CI
// runner (windows.yml, for a pull request labelled store-screenshots) from a real Brouk with made-up data only:
// three profiles ("Client A", "Client B", "Personal") side by side in one
// window, on local demo pages (demo.html) under made-up .example host names.
//
//   node tests/store-screenshots/screenshots.mjs <app folder> <out-dir>
//
// The runner's screen is made 1920×1080 first (the Store wants at least
// 1366×768; the runner starts at 1024×768). Brouk runs with its own data
// folder (BROUK_DEV_DATA_DIR) and the mock keychain. The shots are for a
// person to look at before they go to the Store; nothing here checks them.

import { execFileSync, spawn } from 'node:child_process';
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';

const [appPath, outDir] = process.argv.slice(2);
if (!outDir) {
  console.error('usage: screenshots.mjs <app folder> <out-dir>');
  process.exit(2);
}
mkdirSync(outDir, { recursive: true });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const powershell = (script) =>
  execFileSync('powershell', ['-NoProfile', '-Command', script], { encoding: 'utf8', timeout: 60000 });

// The screen: 1920×1080 through ChangeDisplaySettings (the runner's display
// adapter offers it).
powershell(`
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Display {
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct DEVMODE {
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
    public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra; public int dmFields;
    public int dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
    public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
    public short dmLogPixels; public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
    public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
  }
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool EnumDisplaySettings(string d, int m, ref DEVMODE dm);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int ChangeDisplaySettings(ref DEVMODE dm, int f);
  public static int Set(int w, int h) {
    var dm = new DEVMODE(); dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
    EnumDisplaySettings(null, -1, ref dm);
    dm.dmPelsWidth = w; dm.dmPelsHeight = h; dm.dmFields = 0x80000 | 0x100000;
    return ChangeDisplaySettings(ref dm, 0);
  }
}
'@
"ChangeDisplaySettings: " + [Display]::Set(1920, 1080)`);
console.log(powershell('Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Screen]::PrimaryScreen.Bounds').trim());

// Demo pages on made-up hosts, all served from here (--host-resolver-rules).
const demo = readFileSync(resolve(import.meta.dirname, 'demo.html'), 'utf8');
const server = createServer((request, response) => {
  const host = String(request.headers.host ?? '').split(':')[0];
  response.writeHead(200, { 'content-type': 'text/html; charset=utf-8' });
  response.end(demo.replaceAll('{{HOST}}', host).replaceAll('{{PATH}}', request.url ?? '/'));
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const port = server.address().port;

// The data: three profiles and one maximized window with a group of each.
const data = mkdtempSync(join(tmpdir(), 'brouk-store-'));
const profile = (id, name, color) => {
  mkdirSync(join(data, 'profiles', id), { recursive: true });
  writeFileSync(join(data, 'profiles', id, 'settings.yaml'), `profile:\n  name: ${name}\n  color: ${color}\n`);
};
profile('default', 'Personal', 'green');
profile('p-0000000a', 'Client A', 'blue');
profile('p-0000000b', 'Client B', 'orange');
// Link rules (010) for Settings → Links.
writeFileSync(join(data, 'link-routing.yaml'), `rules:
  - pattern: "*.client-a.example"
    profiles: [p-0000000a]
  - pattern: "*.client-b.example"
    profiles: [p-0000000b]
  - pattern: tickets.example
    profiles: [p-0000000a, p-0000000b]
wrappers:
  - pattern: safelinks.example
    parameter: url
`);
writeFileSync(join(data, 'settings.yaml'), 'appearance:\n  theme: light\nstartup:\n  restore: last_session\n');
const tab = (url, title, id) => `      - url: "${url}"\n        title: "${title}"\n        profile: ${id}\n`;
writeFileSync(join(data, 'session.yaml'), `clean_exit: true
windows:
  - active: 0
    maximized: true
    tabs:
${tab('http://portal.client-a.example/', 'Client A – Portal', 'p-0000000a')}${tab('http://tracker.client-a.example/sprint', 'Sprint board', 'p-0000000a')}${tab('http://admin.client-b.example/', 'Client B – Admin', 'p-0000000b')}${tab('http://docs.client-b.example/api', 'API reference', 'p-0000000b')}${tab('http://news.example/', 'News', 'default')}`);

const executable = join(appPath, 'brouk.exe');
const brouk = spawn(executable, [
  '--use-mock-keychain', `--host-resolver-rules=MAP *.example 127.0.0.1:${port}`,
], { env: { ...process.env, BROUK_DEV_DATA_DIR: data }, stdio: 'ignore' });
const again = (...args) =>
  spawn(executable, args, { env: { ...process.env, BROUK_DEV_DATA_DIR: data }, stdio: 'ignore' });

// The foreground window, without the taskbar (DWM's frame bounds).
function shot(name) {
  const file = join(outDir, `${name}.png`).replaceAll("'", "''");
  console.log(powershell(`
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Win {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int s);
}
'@
$r = New-Object Win+RECT
[void][Win]::DwmGetWindowAttribute([Win]::GetForegroundWindow(), 9, [ref]$r, 16)
$w = $r.R - $r.L; $h = $r.B - $r.T
$i = New-Object System.Drawing.Bitmap $w, $h
[System.Drawing.Graphics]::FromImage($i).CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $h))
$i.Save('${file}')
"${name}: $w x $h"`).trim());
}
// Maximizes the foreground window (the Store wants at least 1366×768).
const maximize = () => powershell(`
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Max {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
}
'@
[void][Max]::ShowWindow([Max]::GetForegroundWindow(), 3)`);
// A left click at (x, y) on the screen, to give a window the keyboard.
const click = (x, y) => powershell(`
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Mouse {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, IntPtr e);
}
'@
[void][Mouse]::SetCursorPos(${x}, ${y}); [Mouse]::mouse_event(2, 0, 0, 0, [IntPtr]::Zero); [Mouse]::mouse_event(4, 0, 0, 0, [IntPtr]::Zero)`);
const keys = (k) => powershell(`Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.SendKeys]::SendWait('${k}')`);

try {
  await sleep(15000);
  shot('1-profiles-side-by-side');
  keys('{F12}');
  await sleep(6000);
  shot('2-developer-tools');
  keys('{F12}');
  await sleep(2000);
  // The task manager (020) opens in its own window with Shift+Esc; the
  // page gets the keyboard first (an empty part of it).
  click(1700, 800);
  await sleep(1000);
  keys('+{ESC}');
  await sleep(5000);
  maximize();
  await sleep(2000);
  shot('3-task-manager');
  keys('%{F4}');
  await sleep(2000);
  // Settings → Links: the link routing rules (010).
  again('brouk://settings/#global:links');
  await sleep(6000);
  maximize();
  await sleep(2000);
  shot('4-link-rules');
} finally {
  brouk.kill();
  try {
    powershell('Get-Process brouk -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue; exit 0');
  } catch {
    // Already gone.
  }
  server.close();
}
