// Smoke tests of a release build (spec 019), run by scripts/release on the
// Intel build copied out of its disk image, and of the Windows app in CI
// (024; <app> is the app folder with brouk.exe):
//
//   node tests/smoke/smoke.mjs <app> <version> <commit> <arch> <out-dir>
//
// Brouk runs with a new, empty data folder, the mock keychain and remote
// debugging on localhost, so the user's own Brouk, data and keychain are
// never touched. The test controls only the copy it started (by its pid).
// Logs and screenshots go to <out-dir>; the exit code is 0 only when every
// check passed.
import { spawn, execFileSync } from 'node:child_process';
import { createServer } from 'node:http';
import { copyFileSync, mkdtempSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync, appendFileSync } from 'node:fs';
import { tmpdir, homedir } from 'node:os';
import { join, extname, resolve } from 'node:path';

const [appPath, version, commit, arch, outDir] = process.argv.slice(2);
if (!outDir) {
  console.error('usage: smoke.mjs <app> <version> <commit> <arch> <out-dir>');
  process.exit(2);
}
const kWindows = process.platform === 'win32';
// The licences page (023) opens in a tab on every platform.
const kInternalPages = true;
const kTotalLimitMs = 5 * 60 * 1000;
const kPagesDir = resolve(import.meta.dirname, '../../docs/testing/pages');
const kCrashReports = join(homedir(), 'Library/Logs/DiagnosticReports');

mkdirSync(outDir, { recursive: true });
const logFile = join(outDir, 'smoke.log');
writeFileSync(logFile, '');
const failures = [];
function log(line) {
  console.log(line);
  appendFileSync(logFile, line + '\n');
}
function check(ok, what) {
  log(`${ok ? 'PASS' : 'FAIL'} ${what}`);
  if (!ok) failures.push(what);
  return ok;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// A test that hangs fails instead of waiting (019: at most 5 minutes).
setTimeout(() => {
  log('FAIL the smoke tests took longer than 5 minutes');
  finish(1);
}, kTotalLimitMs).unref();

let brouk = null;
let server = null;
let dataDir = null;
// The CI runner's screen on Windows, where no one can look: Brouk's own
// window parts (the page screenshots show only pages), and on failure a
// dialog in front of Brouk that explains a hang.
function desktopScreenshot(name) {
  if (!kWindows) return;
  const file = join(outDir, `${name}.png`).replaceAll("'", "''");
  const script = [
    'Add-Type -AssemblyName System.Windows.Forms, System.Drawing',
    '$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds',
    '$i = New-Object System.Drawing.Bitmap $b.Width, $b.Height',
    '[System.Drawing.Graphics]::FromImage($i).CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)',
    `$i.Save('${file}')`,
  ].join('; ');
  try {
    execFileSync('powershell', ['-NoProfile', '-Command', script], { timeout: 30000 });
  } catch (error) {
    log(`(no desktop screenshot: ${error.message})`);
  }
}

// Which control has the keyboard focus (UI Automation), for the log.
const kFocusFunction =
  'function Get-Focus { $f = [System.Windows.Automation.AutomationElement]::FocusedElement; ' +
  'if ($f) { "$($f.Current.ClassName) [$($f.Current.Name)]" } else { "none" } }';

function logFocus(label) {
  if (!kWindows) return;
  const script = ['Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes', kFocusFunction, 'Get-Focus'].join('; ');
  try {
    const output = execFileSync('powershell', ['-NoProfile', '-Command', script], { encoding: 'utf8', timeout: 30000 });
    log(`(focus ${label}: ${output.trim()})`);
  } catch (error) {
    log(`(focus ${label}: ${error.message})`);
  }
}

// Brings Brouk's browser window showing the page titled `title` to the
// front ("<title> - Brouk"): Brouk has several windows (the profile
// manager, tooltips), and keys must reach this one. Returns whether it did.
const frontFunction = (title) =>
  'function Set-BroukFront { (New-Object -ComObject WScript.Shell).AppActivate(' +
  `'${`${title} - Brouk`.replaceAll("'", "''")}') }`;

// Windows: brings Brouk's window to the front and types `keys`
// (System.Windows.Forms.SendKeys syntax: "^f" is Ctrl+F), one after another.
// With `windowsOf`, also lists that process's visible windows afterwards
// and then types `thenKeys`, all from the same PowerShell: starting another
// one may deactivate Brouk, which closes its popups. Returns PowerShell's
// output.
function sendKeys(title, keys, { windowsOf: pid, thenKeys = [], window = ['BroukSuggestions', 'Suggestions'], invoke, afterKeys = [] } = {}) {
  const typed = (list) =>
    list.map((k) => `[System.Windows.Forms.SendKeys]::SendWait('${k.replaceAll("'", "''")}'); Start-Sleep -Milliseconds 300`).join('; ');
  const sends = typed(keys);
  const script = [
    'Add-Type -AssemblyName System.Windows.Forms, UIAutomationClient, UIAutomationTypes',
    kFocusFunction,
    frontFunction(title),
    '$active = Set-BroukFront',
    'Start-Sleep -Milliseconds 500',
    '$before = Get-Focus',
    sends,
    '$after = Get-Focus',
    'Write-Output "Brouk in front: $active, focus before: $before, after: $after"',
    ...(pid && invoke
      ? [
          // `invoke`: a toolbar button of Brouk's window `title`, by its
          // text (an icon glyph's code point), found as a child window and
          // clicked with the mouse, as a user does (no shortcut opens it;
          // UI Automation does not name owner-drawn buttons).
          "Add-Type -Namespace Smoke -Name Click -MemberDefinition '[DllImport(\"user32.dll\")] public static extern bool SetCursorPos(int x, int y); [DllImport(\"user32.dll\")] public static extern void mouse_event(int f, int x, int y, int d, int e); [DllImport(\"user32.dll\", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string n); [DllImport(\"user32.dll\", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string n); [DllImport(\"user32.dll\")] public static extern bool IsWindowVisible(IntPtr h); [DllImport(\"user32.dll\")] public static extern bool GetWindowRect(IntPtr h, out Rect r); public struct Rect { public int Left, Top, Right, Bottom; }'",
          `$top = [Smoke.Click]::FindWindow('BroukBrowserWindow', '${title.replaceAll("'", "''")} - Brouk')`,
          `$b = [Smoke.Click]::FindWindowEx($top, [IntPtr]::Zero, 'Button', [string][char]0x${invoke})`,
          '$invoked = $false',
          'if ($b -ne [IntPtr]::Zero) { $r = New-Object Smoke.Click+Rect; [Smoke.Click]::GetWindowRect($b, [ref]$r) | Out-Null; [Smoke.Click]::SetCursorPos([int](($r.Left + $r.Right) / 2), [int](($r.Top + $r.Bottom) / 2)) | Out-Null; Start-Sleep -Milliseconds 200; [Smoke.Click]::mouse_event(0x0002, 0, 0, 0, 0); [Smoke.Click]::mouse_event(0x0004, 0, 0, 0, 0); $invoked = "$($r.Left),$($r.Top),$($r.Right),$($r.Bottom)" }',
          'Write-Output "invoked: $invoked"',
          // What the window does just after the click, for the log.
          `$seen = @(); for ($i = 0; $i -lt 10; $i++) { Start-Sleep -Milliseconds 150; $h = [Smoke.Click]::FindWindow("${window[0]}", "${window[1]}"); $seen += "$($h -ne [IntPtr]::Zero)/$([Smoke.Click]::IsWindowVisible($h))" }`,
          'Write-Output "after the click: $($seen -join \' \')"',
        ]
      : []),
    ...(pid
      ? [
          'Start-Sleep -Milliseconds 1500',
          `$c = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, ${pid})`,
          '$w = [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c) | ForEach-Object { "$($_.Current.ClassName) [$($_.Current.Name)]" }',
          'Write-Output "windows: $($w -join \', \')"',
          // `window` (class and title): an owned tool window such as the
          // address bar's suggestions, which UI Automation does not list at
          // the top level.
          "Add-Type -Namespace Smoke -Name Win -MemberDefinition '[DllImport(\"user32.dll\", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string n); [DllImport(\"user32.dll\")] public static extern bool IsWindowVisible(IntPtr h);'",
          `$s = [Smoke.Win]::FindWindow("${window[0]}", "${window[1]}")`,
          'Write-Output "window shown: $($s -ne [IntPtr]::Zero -and [Smoke.Win]::IsWindowVisible($s))"',
          ...(thenKeys.length ? [typed(thenKeys), 'Write-Output "then focus: $(Get-Focus)"'] : []),
          // `afterKeys`: sent last, in the same script (e.g. closing what the
          // check opened, while the same window is still in front).
          ...(afterKeys.length ? [typed(afterKeys)] : []),
        ]
      : []),
  ].join('; ');
  // PowerShell can take long to start on a busy runner: one more try.
  for (let attempt = 1; attempt <= 2; attempt++) {
    try {
      const output = execFileSync('powershell', ['-NoProfile', '-Command', script], { encoding: 'utf8', timeout: 60000 });
      log(`(keys ${keys.join(' ')}: ${output.trim()})`);
      return output;
    } catch (error) {
      log(`(keys not sent, attempt ${attempt}: ${error.message})`);
    }
  }
  return '';
}

// Windows: a right-click at screen pixel (x, y).
function rightClick(title, x, y) {
  const script = [
    'Add-Type -Namespace Smoke -Name Mouse -MemberDefinition \'' +
      '[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y); ' +
      '[DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e);\'',
    frontFunction(title),
    'Set-BroukFront | Out-Null',
    'Start-Sleep -Milliseconds 300',
    `[Smoke.Mouse]::SetCursorPos(${x}, ${y}) | Out-Null`,
    'Start-Sleep -Milliseconds 200',
    '[Smoke.Mouse]::mouse_event(0x0008, 0, 0, 0, 0); [Smoke.Mouse]::mouse_event(0x0010, 0, 0, 0, 0)',
    // Away from the tabs again, so no tooltip stays open.
    'Start-Sleep -Milliseconds 200',
    `[Smoke.Mouse]::SetCursorPos(${x}, ${y} + 400) | Out-Null`,
  ].join('; ');
  try {
    execFileSync('powershell', ['-NoProfile', '-Command', script], { timeout: 30000 });
  } catch (error) {
    log(`(no right-click: ${error.message})`);
  }
}

// Windows: whether a control named `name` is in one of the process's
// windows (UI Automation), such as the find bar.
function hasElement(pid, name) {
  const script = [
    'Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes',
    `$pc = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, ${pid})`,
    `$nc = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, '${name}')`,
    '$found = $false',
    'foreach ($top in [System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $pc)) { if ($top.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $nc)) { $found = $true } }',
    'Write-Output $found',
  ].join('; ');
  try {
    return execFileSync('powershell', ['-NoProfile', '-Command', script], { encoding: 'utf8', timeout: 60000 }).trim() === 'True';
  } catch (error) {
    log(`(no UI Automation answer: ${error.message})`);
    return undefined;
  }
}

// The visible top-level windows of a process ("<class> [<name>]").
function windowsOf(pid) {
  const script = [
    'Add-Type -AssemblyName UIAutomationClient',
    `$c = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, ${pid})`,
    '[System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c) | ForEach-Object { "$($_.Current.ClassName) [$($_.Current.Name)]" }',
  ].join('; ');
  try {
    const out = execFileSync('powershell', ['-NoProfile', '-Command', script], { timeout: 60000 });
    return out.toString().trim().split(/\r?\n/);
  } catch (error) {
    log(`(no window list: ${error.message})`);
    return [];
  }
}

// The top-level windows a process still has, for the log.
function logWindowsOf(pid) {
  log(`(windows left: ${windowsOf(pid).join(', ')})`);
}

function finish(code) {
  if (code !== 0) desktopScreenshot('desktop');
  // Chromium's own log, next to the executable on Windows.
  if (code !== 0 && kWindows) {
    try {
      copyFileSync(join(appPath, 'debug.log'), join(outDir, 'debug.log'));
    } catch (error) {
      log(`(no debug.log: ${error.message})`);
    }
  }
  if (brouk && brouk.exitCode === null) brouk.kill('SIGKILL');
  server?.close();
  try {
    if (dataDir) rmSync(dataDir, { recursive: true, force: true });
  } catch (error) {
    // Windows may still hold files of the process just ended.
    log(`(data folder not removed: ${error.message})`);
  }
  log(code === 0 ? 'All smoke tests passed.' : `${failures.length} smoke test(s) failed.`);
  process.exit(code);
}

// Windows: closes the windows of `child` as the user does. Without /F,
// taskkill asks one of the process's windows to close (WM_CLOSE); Brouk runs
// until its last window has closed, so ask again while it still has windows
// (profile windows, the manager).
async function closeWindowsOf(child) {
  for (let i = 0; i < 10 && child.exitCode === null; ++i) {
    try {
      execFileSync('taskkill', ['/PID', String(child.pid)], { stdio: 'ignore' });
    } catch {
      // No window left to ask: Brouk is quitting.
    }
    await sleep(500);
  }
}

// ---- Chrome DevTools Protocol ----

class Cdp {
  #ws;
  #id = 0;
  #pending = new Map();
  static async connect(url) {
    const cdp = new Cdp();
    cdp.#ws = new WebSocket(url);
    cdp.#ws.onmessage = (event) => {
      const message = JSON.parse(event.data);
      const done = cdp.#pending.get(message.id);
      if (done) {
        cdp.#pending.delete(message.id);
        done(message);
      }
    };
    await new Promise((ok, fail) => {
      cdp.#ws.onopen = ok;
      cdp.#ws.onerror = fail;
    });
    return cdp;
  }
  send(method, params = {}, timeoutMs = 15000) {
    const id = ++this.#id;
    return new Promise((ok, fail) => {
      const timer = setTimeout(() => {
        this.#pending.delete(id);
        fail(new Error(`${method} timed out`));
      }, timeoutMs);
      this.#pending.set(id, (message) => {
        clearTimeout(timer);
        if (message.error) fail(new Error(`${method}: ${message.error.message}`));
        else ok(message.result);
      });
      this.#ws.send(JSON.stringify({ id, method, params }));
    });
  }
  close() {
    this.#ws.close();
  }
}

// Brouk's DevTools endpoint; a Brouk that hangs fails the request instead
// of the whole run.
async function devtoolsJson(port, path) {
  return (await fetch(`http://127.0.0.1:${port}${path}`, { signal: AbortSignal.timeout(5000) })).json();
}

async function pageTarget(port) {
  const list = await devtoolsJson(port, '/json/list');
  return list.find((t) => t.type === 'page' && !t.url.startsWith('brouk:') && !t.url.startsWith('devtools:'));
}

async function screenshot(cdp, name) {
  try {
    const { data } = await cdp.send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(join(outDir, `${name}.png`), Buffer.from(data, 'base64'));
  } catch (error) {
    log(`(no screenshot for ${name}: ${error.message})`);
  }
}

async function waitForTitle(cdp, expected, timeoutMs = 15000) {
  const end = Date.now() + timeoutMs;
  let title = '';
  while (Date.now() < end) {
    try {
      const { result } = await cdp.send('Runtime.evaluate', {
        expression: 'document.readyState === "complete" ? document.title : ""',
        returnByValue: true,
      });
      title = result.value ?? '';
      if (title === expected) return title;
    } catch {
      // The page is between documents.
    }
    await sleep(250);
  }
  return title;
}

// ---- Test pages, served on localhost ----

const kTypes = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json', '.css': 'text/css' };
function startServer() {
  return new Promise((ok) => {
    const s = createServer((request, response) => {
      const path = decodeURIComponent(new URL(request.url, 'http://x').pathname);
      if (path === '/sign-in') {
        // HTTP basic authentication (012): the browser asks for a login.
        response.writeHead(401, { 'WWW-Authenticate': 'Basic realm="Smoke test"' }).end('Sign in');
        return;
      }
      const file = resolve(kPagesDir, '.' + path);
      if (!file.startsWith(kPagesDir)) {
        response.writeHead(403).end();
        return;
      }
      try {
        const body = readFileSync(file);
        response.writeHead(200, { 'Content-Type': kTypes[extname(file)] ?? 'application/octet-stream' });
        response.end(body);
      } catch {
        response.writeHead(404).end();
      }
    });
    s.listen(0, '127.0.0.1', () => ok(s));
  });
}

function crashReports() {
  if (kWindows) return []; // Windows Error Reporting keeps no reports here.
  try {
    // The main process only: the renderer crash is caused on purpose.
    return readdirSync(kCrashReports).filter((name) => /^brouk[-_]/i.test(name));
  } catch {
    return [];
  }
}

// ---- The tests ----

// Windows, passwords (008): signing in on the test page offers to save the
// login (the prompt under the key icon, with its Save button).
async function savePasswordFromLogin({ executable, port, pagesBase }) {
  const loginUrl = `${pagesBase}/008/login.html`;
  spawn(executable, [loginUrl], { env: { ...process.env, BROUK_DEV_DATA_DIR: dataDir }, stdio: 'ignore' });
  let login;
  for (let i = 0; i < 30 && !login; i++) {
    await sleep(500);
    login = await devtoolsJson(port, '/json/list')
      .then((list) => list.find((t) => t.type === 'page' && t.url === loginUrl))
      .catch(() => undefined);
  }
  if (!check(login !== undefined, 'a second start opens the login test page')) return;
  const page = await Cdp.connect(login.webSocketDebuggerUrl);
  const evaluate = async (expression) =>
    (await page.send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })).result.value;
  // The page is listed as soon as it starts loading: the page script
  // tracks the form's fields once it has loaded.
  for (let i = 0; i < 20; i++) {
    if ((await evaluate('document.readyState').catch(() => '')) === 'complete') break;
    await sleep(250);
  }
  await sleep(1000);
  // The page gets focus and input events as in a window in front (the
  // smoke test's windows take turns in front).
  await page.send('Emulation.setFocusEmulationEnabled', { enabled: true });
  // Typed as the user types (the page script reads what was typed).
  await evaluate("document.querySelector('#email').focus()");
  await page.send('Input.insertText', { text: 'smoke@example.com' });
  await evaluate("document.querySelector('#pw').focus()");
  await page.send('Input.insertText', { text: 'S3cret-smoke!' });
  await evaluate("document.querySelector('button').click()");
  // The prompt comes once the next page loads; it is served from this
  // process, which the PowerShell call below blocks: loaded first.
  for (let i = 0; i < 20; i++) {
    await sleep(500);
    const targets = await devtoolsJson(port, '/json/list').catch(() => []);
    if (targets.some((t) => t.id === login.id && t.url.includes('/008/done.html'))) break;
  }
  // The prompt is an owned window with a Save button.
  const script = [
    "Add-Type -Namespace Smoke -Name Prompt -MemberDefinition '" +
      '[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string n); ' +
      '[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string n); ' +
      "[DllImport(\"user32.dll\")] public static extern bool IsWindowVisible(IntPtr h);'",
    '$prompt = [IntPtr]::Zero',
    "for ($i = 0; $i -lt 30 -and ($prompt -eq [IntPtr]::Zero -or -not [Smoke.Prompt]::IsWindowVisible($prompt)); $i++) { Start-Sleep -Milliseconds 500; $prompt = [Smoke.Prompt]::FindWindow('BroukPasswordPrompt', 'Save password?') }",
    'Write-Output "prompt shown: $($prompt -ne [IntPtr]::Zero -and [Smoke.Prompt]::IsWindowVisible($prompt))"',
    "$save = [Smoke.Prompt]::FindWindowEx($prompt, [IntPtr]::Zero, 'Button', 'Save')",
    'if ($save -ne [IntPtr]::Zero) { Write-Output "save found" }',
  ].join('; ');
  let output = '';
  try {
    output = execFileSync('powershell', ['-NoProfile', '-Command', script], { encoding: 'utf8', timeout: 60000 });
  } catch (error) {
    log(`(no prompt answer: ${error.message})`);
  }
  log(`(${output.trim().replaceAll(/\r?\n/g, '; ')})`);
  check(/prompt shown: True/.test(output), 'signing in on the login page offers to save the password');
  check(/save found/.test(output), 'the save prompt has a Save button');
  // Saving and filling are tested by hand: a click sent to the prompt's
  // button while Brouk is not the active app does not always arrive.
  page.close();
}

// Windows, autofill (009): see the call in main().
async function autofillFromEntry({ executable, port, pagesBase, pid }) {
  const targets = await devtoolsJson(port, '/json/list').catch(() => []);
  const settings = targets.find((t) => t.type === 'page' && t.url.startsWith('brouk://settings'));
  if (!check(settings !== undefined, 'the settings window is open for autofill')) return;
  const settingsPage = await Cdp.connect(settings.webSocketDebuggerUrl);
  const { result } = await settingsPage.send('Runtime.evaluate', {
    expression:
      "new Promise((ok) => window.cefQuery({ request: JSON.stringify({ type: 'autofill.saveEntry', id: 0, label: 'Smoke', " +
      "values: { first_name: 'Jana', last_name: 'Smoke', email: 'jana@example.com', city: 'Brno' } }), " +
      "onSuccess: (r) => ok('ok ' + r), onFailure: (c, m) => ok('failed ' + m) }))",
    awaitPromise: true,
    returnByValue: true,
  });
  settingsPage.close();
  if (!check(String(result.value).startsWith('ok'), `Settings → Autofill saves an entry (${result.value})`)) return;
  const url = `${pagesBase}/009/checkout.html`;
  spawn(executable, [url], { env: { ...process.env, BROUK_DEV_DATA_DIR: dataDir }, stdio: 'ignore' });
  let target;
  for (let i = 0; i < 30 && !target; i++) {
    await sleep(500);
    target = await devtoolsJson(port, '/json/list')
      .then((list) => list.find((t) => t.type === 'page' && t.url === url))
      .catch(() => undefined);
  }
  if (!check(target !== undefined, 'a second start opens the checkout test page')) return;
  const page = await Cdp.connect(target.webSocketDebuggerUrl);
  const evaluate = async (expression) =>
    (await page.send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true })).result.value;
  await sleep(1000);  // The page script reports the form first.
  await evaluate("document.querySelector('[name=jmeno]').focus()");
  // The list is an owned window that never takes the keyboard: its first
  // row (the entry) is clicked with the mouse.
  const script = [
    // One C# source: a type of an earlier Add-Type cannot be referenced.
    "Add-Type -TypeDefinition 'using System; using System.Runtime.InteropServices; namespace Smoke { " +
      'public struct Rect { public int Left, Top, Right, Bottom; } ' +
      'public static class List { ' +
      '[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindow(string c, string n); ' +
      '[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h); ' +
      '[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r); ' +
      '[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y); ' +
      "[DllImport(\"user32.dll\")] public static extern void mouse_event(int f, int x, int y, int d, int e); } }'",
    '$list = [IntPtr]::Zero',
    "for ($i = 0; $i -lt 20 -and ($list -eq [IntPtr]::Zero -or -not [Smoke.List]::IsWindowVisible($list)); $i++) { Start-Sleep -Milliseconds 500; $list = [Smoke.List]::FindWindow('BroukPasswordList', 'Passwords') }",
    'Write-Output "list shown: $($list -ne [IntPtr]::Zero -and [Smoke.List]::IsWindowVisible($list))"',
    '$r = New-Object Smoke.Rect',
    'if ([Smoke.List]::GetWindowRect($list, [ref]$r)) { [Smoke.List]::SetCursorPos($r.Left + 60, $r.Top + 20) | Out-Null; Start-Sleep -Milliseconds 300; [Smoke.List]::mouse_event(0x0002, 0, 0, 0, 0); [Smoke.List]::mouse_event(0x0004, 0, 0, 0, 0); Write-Output "row clicked" }',
  ].join('; ');
  let output = '';
  try {
    output = execFileSync('powershell', ['-NoProfile', '-Command', script], { encoding: 'utf8', timeout: 60000 });
  } catch (error) {
    log(`(no list answer: ${error.message})`);
  }
  log(`(${output.trim().replaceAll(/\r?\n/g, '; ')})`);
  check(/list shown: True/.test(output), 'focusing a form field shows the autofill drop-down');
  let filled = '';
  for (let i = 0; i < 20 && filled !== 'Smoke|Brno'; i++) {
    await sleep(500);
    filled = (await evaluate("[...['prijmeni', 'mesto']].map((n) => document.querySelector(`[name=${n}]`).value).join('|')").catch(() => '')) ?? '';
  }
  check(filled === 'Smoke|Brno', `choosing the entry fills the form (${filled})`);
  page.close();
}

// Windows, ad blocking (007): see the call in main().
async function adblockMyFilter({ port, pagesBase, pid }) {
  const targets = await devtoolsJson(port, '/json/list').catch(() => []);
  const settings = targets.find((t) => t.type === 'page' && t.url.startsWith('brouk://settings'));
  const front = targets.find((t) => t.type === 'page' && t.url === `${pagesBase}/009/checkout.html`);
  if (!check(settings !== undefined && front !== undefined, 'the settings window and a test page are open for ad blocking')) return;
  const settingsPage = await Cdp.connect(settings.webSocketDebuggerUrl);
  const { result } = await settingsPage.send('Runtime.evaluate', {
    expression:
      "new Promise((ok) => window.cefQuery({ request: JSON.stringify({ type: 'adblock.saveMyFilters', text: '/brouk-smoke-ad.png' }), " +
      "onSuccess: (r) => ok('ok ' + r), onFailure: (c, m) => ok('failed ' + m) }))",
    awaitPromise: true,
    returnByValue: true,
  });
  settingsPage.close();
  if (!check(String(result.value).startsWith('ok'), `My filters takes a rule (${result.value})`)) return;
  const page = await Cdp.connect(front.webSocketDebuggerUrl);
  // Blocked requests fail; others get the server's 404.
  let answer = '';
  for (let i = 0; i < 30 && answer !== 'blocked'; i++) {
    await sleep(500);
    const { result: fetched } = await page.send('Runtime.evaluate', {
      expression: `fetch('/brouk-smoke-ad.png?n=${i}').then(() => 'loaded', () => 'blocked')`,
      awaitPromise: true,
      returnByValue: true,
    });
    answer = fetched.value ?? '';
  }
  page.close();
  check(answer === 'blocked', `the rule blocks the page's request (${answer})`);
  let counted = false;
  for (let i = 0; i < 10 && !counted; i++) {
    await sleep(500);
    counted = [1, 2, 3].some((n) => hasElement(pid, `${n} blocked on this page`));
  }
  check(counted, 'the shield counts the blocked request');
}

async function main() {
  const executable = kWindows ? join(appPath, 'brouk.exe') : join(appPath, 'Contents/MacOS/brouk');
  const started = Date.now();
  const reportsBefore = new Set(crashReports());

  const printed = execFileSync(executable, ['--version'], { encoding: 'utf8' }).trim();
  check(printed === `Brouk ${version} (${commit})`, `--version prints "Brouk ${version} (${commit})" (got "${printed}")`);

  if (kWindows) {
    // Windows shows this as the app's name (taskbar menu, Task Manager).
    const description = execFileSync(
      'powershell',
      ['-NoProfile', '-Command', `(Get-Item -LiteralPath '${executable.replaceAll("'", "''")}').VersionInfo.FileDescription`],
      { encoding: 'utf8' },
    ).trim();
    check(description === 'Brouk', `brouk.exe is described as "Brouk" (got "${description}")`);
  }

  if (!kWindows) {
    const feed = execFileSync('plutil', ['-extract', 'SUFeedURL', 'raw', '-o', '-', join(appPath, 'Contents/Info.plist')], {
      encoding: 'utf8',
    }).trim();
    check(feed.endsWith(`/appcast-${arch}.xml`), `the updater points to the ${arch} feed (${feed})`);
  }

  server = await startServer();
  const pagesBase = `http://127.0.0.1:${server.address().port}`;

  dataDir = mkdtempSync(join(tmpdir(), 'brouk-smoke-'));
  const port = 9400 + Math.floor(Math.random() * 500);
  // A blank tab for the web pages, and the licences page (023): URLs with
  // --profile open as typed, without link routing.
  const args = ['--use-mock-keychain', `--remote-debugging-port=${port}`, '--profile=smoke', 'about:blank'];
  if (kInternalPages) args.push('brouk://credits');
  // The profile manager (005) in its own window; on macOS it is opened from the menu.
  if (kWindows) args.push('brouk://profiles');
  // Settings (004) in its own window too.
  if (kWindows) args.push('brouk://settings');
  brouk = spawn(executable, args, {
    env: { ...process.env, BROUK_DEV_DATA_DIR: dataDir },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  const appLog = join(outDir, 'brouk.log');
  writeFileSync(appLog, '');
  brouk.stdout.on('data', (chunk) => appendFileSync(appLog, chunk));
  brouk.stderr.on('data', (chunk) => appendFileSync(appLog, chunk));
  log(`Started Brouk (pid ${brouk.pid}), data in ${dataDir}`);

  let target = null;
  let lastError = '';
  for (let i = 0; i < 60 && !target && brouk.exitCode === null; i++) {
    await sleep(500);
    target = await pageTarget(port).catch((error) => {
      lastError = error.message;
      return null;
    });
  }
  if (!target) log(`(no tab: exit code ${brouk.exitCode}, last error: ${lastError || 'none'})`);
  if (!check(target !== null, 'Brouk starts and opens a tab')) return finish(1);
  await sleep(Math.max(0, 10000 - (Date.now() - started)));
  check(brouk.exitCode === null, 'Brouk is still running after 10 seconds');
  desktopScreenshot('window');

  const cdp = await Cdp.connect(target.webSocketDebuggerUrl);
  await cdp.send('Page.enable');
  const pages = readdirSync(kPagesDir).filter((name) => name.endsWith('.html') && name !== 'crash-and-hang.html');
  for (const name of pages) {
    const expected = /<title>([^<]*)<\/title>/.exec(readFileSync(join(kPagesDir, name), 'utf8'))?.[1] ?? '';
    await cdp.send('Page.navigate', { url: `${pagesBase}/${name}` });
    const title = await waitForTitle(cdp, expected);
    check(title === expected, `${name} loads ("${title}")`);
    await screenshot(cdp, name.replace('.html', ''));
  }

  // The open-source licences (023) are in the app and load, with
  // Chromium's credits next to them.
  const list = await devtoolsJson(port, '/json/list');
  const creditsTarget = list.find((t) => t.url.startsWith('brouk://credits'));
  if (kInternalPages && check(creditsTarget !== undefined, 'the licences page opens')) {
    const credits = await Cdp.connect(creditsTarget.webSocketDebuggerUrl);
    const title = await waitForTitle(credits, 'Open-Source Licences');
    let shown = '';
    for (let i = 0; i < 40 && !/components$/.test(shown); i++) {
      const { result } = await credits.send('Runtime.evaluate', {
        expression: 'document.getElementById("error").hidden ? document.getElementById("count").textContent : "error"',
        returnByValue: true,
      });
      shown = result.value ?? '';
      if (shown === 'error') break;
      await sleep(250);
    }
    check(title === 'Open-Source Licences' && /^\d+ components$/.test(shown), `brouk://credits lists the licences ("${shown}")`);
    await screenshot(credits, 'credits');
    await credits.send('Page.navigate', { url: 'brouk://credits/chromium' });
    const chromium = await waitForTitle(credits, 'Credits');
    check(chromium === 'Credits', `brouk://credits/chromium shows Chromium's credits ("${chromium}")`);
    credits.close();
  }

  // Find in page and zoom (Windows app, 024), with real key presses sent
  // to Brouk's window: Ctrl+Plus twice, Ctrl+F, then "beetle" in the find
  // bar. A screenshot of the find bar and the zoom indicator for a look.
  if (kWindows) {
    await cdp.send('Page.navigate', { url: `${pagesBase}/find-and-zoom.html` });
    await waitForTitle(cdp, 'Find and zoom test');
    const zoom = async () =>
      (await cdp.send('Runtime.evaluate', { expression: 'Math.round(devicePixelRatio * 100)' })).result.value;
    const before = await zoom();
    // Each zoom step reaches the page a moment later: wait up to 3 seconds.
    const zoomTo = async (expected) => {
      let now = await zoom();
      for (let i = 0; i < 15 && now !== expected; i++) {
        await sleep(200);
        now = await zoom();
      }
      return now;
    };
    // One key at a time: a busy runner loses keys sent in a quick row, and
    // sometimes one right after activating the window, so a key without
    // effect is sent once more (a broken shortcut still fails twice).
    const pressUntil = async (keys, expected) => {
      sendKeys('Find and zoom test', keys);
      let now = await zoomTo(expected);
      if (now !== expected) {
        log(`(zoom ${now} after ${keys.join(' ')}, not ${expected}: sending again)`);
        sendKeys('Find and zoom test', keys);
        now = await zoomTo(expected);
      }
      return now;
    };
    const first = await pressUntil(['^{ADD}'], Math.round(before * 1.1));
    const zoomed = await pressUntil(['^{ADD}'], Math.round(before * 1.25));
    check(zoomed === Math.round(before * 1.25), `Ctrl+Plus twice zooms to 125 % (${before} → ${first} → ${zoomed})`);
    sendKeys('Find and zoom test', ['^f', 'beetle']);
    await sleep(1000);
    logFocus('after typing in the find bar');
    desktopScreenshot('find-bar');
    logFocus('after the screenshot');
    // Escape closes the find bar, also when the page has the focus (as in
    // Chrome; the screenshot can leave the focus there), then Ctrl+0 resets
    // the zoom.
    const findOpen = hasElement(brouk.pid, 'Find in page');
    sendKeys('Find and zoom test', ['{ESC}']);
    await sleep(500);
    const findClosed = hasElement(brouk.pid, 'Find in page') === false;
    check(findOpen === true && findClosed, `Escape closes the find bar (open before: ${findOpen})`);
    const reset = await pressUntil(['^0'], before);
    check(reset === before, `Ctrl+0 resets the zoom (${reset})`);
    desktopScreenshot('find-bar-closed');
  }

  // HTML full screen (Windows app, 024): the window covers the screen
  // without tab strip and toolbar; Escape (a real key press) leaves it.
  if (kWindows) {
    await cdp.send('Page.navigate', { url: `${pagesBase}/fullscreen.html` });
    await waitForTitle(cdp, 'Full screen test');
    await cdp.send('Runtime.evaluate', {
      expression: 'document.documentElement.requestFullscreen().then(() => "yes", (e) => String(e))',
      userGesture: true,
      awaitPromise: true,
    });
    await sleep(1500);
    desktopScreenshot('fullscreen');
    sendKeys('Full screen test', ['{ESC}']);
    await sleep(1000);
    const { result } = await cdp.send('Runtime.evaluate', { expression: 'document.fullscreenElement === null' });
    check(result.value === true, 'Escape leaves HTML full screen');
    desktopScreenshot('fullscreen-left');
  }

  // Windows app (024): the page's blocked popup shows the "Pop-ups blocked"
  // indicator in the address bar; an HTTP sign-in shows Chromium's login
  // dialog. Screenshots of both for a look.
  if (kWindows) {
    await cdp.send('Page.navigate', { url: `${pagesBase}/popups.html` });
    await waitForTitle(cdp, 'Popups test');
    await sleep(1500);
    desktopScreenshot('popup-blocked');
    await cdp.send('Page.navigate', { url: `${pagesBase}/sign-in` });
    await sleep(2000);
    desktopScreenshot('sign-in');
  }

  // Menus (Windows app, 024): the tab menu on a right-click on the first
  // tab, and the page's context menu (Shift+F10). Screenshots for a look.
  if (kWindows) {
    await cdp.send('Page.navigate', { url: `${pagesBase}/context-menu.html` });
    await waitForTitle(cdp, 'Context menu test');
    rightClick('Context menu test', 200, 22);
    await sleep(800);
    desktopScreenshot('tab-menu');
    sendKeys('Context menu test', ['{ESC}', '+{F10}']);
    await sleep(800);
    desktopScreenshot('page-menu');
    sendKeys('Context menu test', ['{ESC}']);
  }

  // Screen sharing (Windows app, 024): getDisplayMedia() shows Brouk's
  // picker over the tab (screenshot); Escape cancels it and the page gets
  // an error instead of a stream.
  if (kWindows) {
    await cdp.send('Page.navigate', { url: `${pagesBase}/screen-share.html` });
    await waitForTitle(cdp, 'Screen sharing test');
    await cdp.send('Runtime.evaluate', {
      expression:
        'window.shareResult = "pending"; navigator.mediaDevices.getDisplayMedia({ video: true }).then(' +
        '(s) => { window.shareResult = "shared"; s.getTracks().forEach((t) => t.stop()); }, ' +
        '(e) => { window.shareResult = e.name; })',
      userGesture: true,
    });
    await sleep(1500);
    desktopScreenshot('screen-picker');
    sendKeys('Screen sharing test', ['{ESC}']);
    await sleep(1000);
    const { result } = await cdp.send('Runtime.evaluate', { expression: 'window.shareResult' });
    check(result.value !== 'pending' && result.value !== 'shared', `Escape cancels the screen picker (${result.value})`);
  }

  // The Windows app draws the permission prompt and the dialogs over the
  // page itself (024): a screenshot of both for a look. The navigation
  // below must then take the open dialog away.
  if (kWindows) {
    await cdp.send('Page.navigate', { url: `${pagesBase}/dialogs.html` });
    await waitForTitle(cdp, 'Dialogs test');
    await cdp.send('Runtime.evaluate', {
      expression:
        'navigator.geolocation.getCurrentPosition(() => {}, () => {}); ' +
        'setTimeout(() => prompt("Smoke test question", "An answer"), 500)',
    });
    await sleep(2500);
    desktopScreenshot('dialogs');
  }

  // Profiles (Windows app, 024 W2): the profile manager is open in its own
  // window; a profile created there opens in a new window, in its group.
  if (kWindows) {
    const all = await devtoolsJson(port, '/json/list');
    const profilesTarget = all.find((t) => t.url.startsWith('brouk://profiles'));
    if (check(profilesTarget !== undefined, 'the profile manager opens')) {
      const manager = await Cdp.connect(profilesTarget.webSocketDebuggerUrl);
      const title = await waitForTitle(manager, 'Profiles');
      // The page's own bridge, as its buttons use it.
      const ask = async (request) => {
        const { result } = await manager.send('Runtime.evaluate', {
          expression:
            'new Promise((ok) => window.cefQuery({ request: ' +
            JSON.stringify(JSON.stringify(request)) +
            ", onSuccess: (r) => ok('ok ' + r), onFailure: (c, m) => ok('failed ' + m) }))",
          awaitPromise: true,
          returnByValue: true,
        });
        return result.value ?? '';
      };
      const created = await ask({ type: 'profiles.create', name: 'Work', color: 'blue' });
      check(title === 'Profiles' && created.startsWith('ok '), `a profile can be created (${created})`);
      if (created.startsWith('ok ')) {
        const opened = await ask({ type: 'profiles.open', id: JSON.parse(created.slice(3)).id });
        check(opened.startsWith('ok'), `the new profile opens in a window (${opened})`);
        await sleep(2000);
        desktopScreenshot('profiles');
      }
      manager.close();
    }
  }

  // Settings (Windows app, 024 W2): brouk://settings opens in its own
  // window, and a change made in settings.yaml applies while Brouk runs
  // (004, live reload): the pages' colour scheme follows appearance.theme.
  if (kWindows) {
    const list = await devtoolsJson(port, '/json/list');
    const settingsTarget = list.find((t) => t.url.startsWith('brouk://settings'));
    if (check(settingsTarget !== undefined, 'the settings window opens')) {
      const settingsPage = await Cdp.connect(settingsTarget.webSocketDebuggerUrl);
      const title = await waitForTitle(settingsPage, 'Settings');
      check(title === 'Settings', `brouk://settings loads ("${title}")`);
      await screenshot(settingsPage, 'settings');
      // In the settings window, which is in front: pages in windows behind
      // it may not answer quickly on a busy runner.
      const dark = async () =>
        (
          await settingsPage.send('Runtime.evaluate', {
            expression: "matchMedia('(prefers-color-scheme: dark)').matches",
            returnByValue: true,
          })
        ).result.value;
      // The theme Brouk has read, through the page's own bridge.
      const theme = async () => {
        const { result } = await settingsPage.send('Runtime.evaluate', {
          expression:
            "new Promise((ok) => window.cefQuery({ request: JSON.stringify({ type: 'settings.state' }), " +
            "onSuccess: (r) => ok(JSON.parse(r).global.find((i) => i.key === 'appearance.theme')?.value ?? ''), " +
            "onFailure: (c, m) => ok('failed ' + m) }))",
          awaitPromise: true,
          returnByValue: true,
        });
        return result.value ?? '';
      };
      const before = await dark();
      const wanted = before ? 'light' : 'dark';
      const settingsFile = join(dataDir, 'settings.yaml');
      writeFileSync(settingsFile, `appearance:\n  theme: ${wanted}\n`);
      let read = await theme();
      for (let i = 0; i < 25 && read !== wanted; i++) {
        await sleep(200);
        read = await theme();
      }
      check(read === wanted, `Brouk reads settings.yaml again after it changed ("${read}")`);
      let now = before;
      for (let i = 0; i < 15 && now === before; i++) {
        await sleep(200);
        now = await dark();
      }
      check(now === !before, `the pages' colour scheme follows appearance.theme (dark: ${before} → ${now})`);
      desktopScreenshot('theme');
      writeFileSync(settingsFile, 'appearance:\n  theme: system\n');
      await sleep(1000);
      settingsPage.close();
    }
  }

  // Downloads (014): a download goes to the downloads folder without
  // asking (here one in the test's data folder), and Ctrl+J shows it on
  // the downloads page.
  if (kWindows) {
    const folder = join(dataDir, 'downloaded');
    mkdirSync(folder, { recursive: true });
    writeFileSync(join(dataDir, 'settings.yaml'), `appearance:\n  theme: system\ndownloads:\n  folder: '${folder}'\n`);
    await sleep(1500); // Live reload.
    await cdp.send('Page.navigate', { url: `${pagesBase}/downloads.html` });
    await waitForTitle(cdp, 'Downloads test');
    const { result } = await cdp.send('Runtime.evaluate', {
      expression: "JSON.stringify(document.querySelector('a[download]').getBoundingClientRect())",
      returnByValue: true,
    });
    const link = JSON.parse(result.value ?? '{}');
    const x = (link.left ?? 0) + 5;
    const y = (link.top ?? 0) + 5;
    for (const type of ['mousePressed', 'mouseReleased']) {
      await cdp.send('Input.dispatchMouseEvent', { type, x, y, button: 'left', clickCount: 1 });
    }
    const file = join(folder, 'hello.txt');
    let saved = false;
    for (let i = 0; i < 20 && !saved; i++) {
      await sleep(500);
      try {
        saved = readFileSync(file, 'utf8') === 'Hello from Brouk';
      } catch {
        // Not there yet.
      }
    }
    check(saved, 'a download is saved to the downloads folder without asking');
    // The downloads button shows the list under it (Chrome's bubble);
    // Escape closes it.
    const list = sendKeys('Downloads test', [], {
      windowsOf: brouk.pid,
      invoke: 'E896', // The downloads button's arrow.
      thenKeys: ['{ESC}'],
      window: ['BroukDownloads', 'Downloads'],
    });
    check(/window shown: True/.test(list), 'the downloads button shows the list of downloads');
    sendKeys('Downloads test', ['^j']);
    let downloadsTarget;
    for (let i = 0; i < 20 && !downloadsTarget; i++) {
      await sleep(500);
      downloadsTarget = await devtoolsJson(port, '/json/list')
        .then((list) => list.find((t) => t.type === 'page' && t.url.startsWith('brouk://downloads')))
        .catch(() => undefined);
    }
    if (check(downloadsTarget !== undefined, 'Ctrl+J opens the downloads page')) {
      const downloadsPage = await Cdp.connect(downloadsTarget.webSocketDebuggerUrl);
      let listed = false;
      for (let i = 0; i < 20 && !listed; i++) {
        await sleep(500);
        const { result: text } = await downloadsPage.send('Runtime.evaluate', {
          expression: 'document.body.innerText',
          returnByValue: true,
        });
        listed = (text.value ?? '').includes('hello.txt');
      }
      check(listed, 'the downloads page lists the download');
      downloadsPage.close();
      desktopScreenshot('downloaded');
      // Back to the test page's tab for the tests that follow.
      sendKeys('Downloads', ['^w']);
    }

    // Bookmarks (011): Ctrl+D bookmarks the page at once (the bubble
    // opens over it), and Ctrl+Shift+O lists it in the bookmark manager.
    sendKeys('Downloads test', ['^d', '{ENTER}']);
    const bookmarksFile = join(dataDir, 'bookmarks.json');
    let bookmarked = false;
    for (let i = 0; i < 20 && !bookmarked; i++) {
      await sleep(500);
      try {
        bookmarked = readFileSync(bookmarksFile, 'utf8').includes('/downloads.html');
      } catch {
        // Not written yet.
      }
    }
    check(bookmarked, 'Ctrl+D bookmarks the page (bookmarks.json)');
    sendKeys('Downloads test', ['^+o']);
    let managerTarget;
    for (let i = 0; i < 20 && !managerTarget; i++) {
      await sleep(500);
      managerTarget = await devtoolsJson(port, '/json/list')
        .then((list) => list.find((t) => t.type === 'page' && t.url.startsWith('brouk://bookmarks')))
        .catch(() => undefined);
    }
    if (check(managerTarget !== undefined, 'Ctrl+Shift+O opens the bookmark manager')) {
      const manager = await Cdp.connect(managerTarget.webSocketDebuggerUrl);
      let listed = false;
      for (let i = 0; i < 20 && !listed; i++) {
        await sleep(500);
        const { result: text } = await manager.send('Runtime.evaluate', {
          expression: 'document.body.innerText',
          returnByValue: true,
        });
        listed = (text.value ?? '').includes('Downloads test');
      }
      check(listed, 'the bookmark manager lists the new bookmark');
      manager.close();
      desktopScreenshot('bookmarks');
      sendKeys('Bookmarks', ['^w']);
    }

    // Ctrl+Shift+B shows the bookmarks bar on every tab, kept as a setting.
    sendKeys('Downloads test', ['^+b']);
    let always = false;
    for (let i = 0; i < 20 && !always; i++) {
      await sleep(500);
      try {
        always = /bar: "?always"?/.test(readFileSync(join(dataDir, 'settings.yaml'), 'utf8'));
      } catch {
        // Not written yet.
      }
    }
    check(always, 'Ctrl+Shift+B always shows the bookmarks bar (settings.yaml)');
    await sleep(500);
    desktopScreenshot('bookmarks-bar');
    sendKeys('Downloads test', ['^+b']);

    // Task manager (020): Shift+Esc opens its window, which lists the
    // tabs; pausing a tab there shows in its row, and Resume undoes it.
    sendKeys('Downloads test', ['+{ESC}']);
    let tasksTarget;
    for (let i = 0; i < 20 && !tasksTarget; i++) {
      await sleep(500);
      tasksTarget = await devtoolsJson(port, '/json/list')
        .then((list) => list.find((t) => t.type === 'page' && t.url.startsWith('brouk://task-manager')))
        .catch(() => undefined);
    }
    if (check(tasksTarget !== undefined, 'Shift+Esc opens the task manager')) {
      const tasks = await Cdp.connect(tasksTarget.webSocketDebuggerUrl);
      const ask = async (request) => {
        const { result } = await tasks.send('Runtime.evaluate', {
          expression:
            'new Promise((ok) => window.cefQuery({ request: ' +
            JSON.stringify(JSON.stringify(request)) +
            ", onSuccess: (r) => ok('ok ' + r), onFailure: (c, m) => ok('failed ' + m) }))",
          awaitPromise: true,
          returnByValue: true,
        });
        return result.value ?? '';
      };
      const pageOf = async () => {
        const answer = await ask({ type: 'tasks.list', jsMemory: false });
        if (!answer.startsWith('ok ')) return undefined;
        const pages = JSON.parse(answer.slice(3)).pages ?? {};
        const key = Object.keys(pages).find((k) => (pages[k].url ?? '').includes('/downloads.html'));
        return key ? { key, ...pages[key] } : undefined;
      };
      const row = await pageOf();
      if (check(row !== undefined, 'the task manager lists the tabs')) {
        await ask({ type: 'tasks.action', action: 'pause', pages: [row.key] });
        const paused = await pageOf();
        check(paused?.paused === true, 'Pause in the task manager pauses the tab');
        desktopScreenshot('task-manager');
        await ask({ type: 'tasks.action', action: 'resume', pages: [row.key] });
        const resumed = await pageOf();
        check(resumed?.paused === false, 'Resume runs it again');
        // A resumed page takes keys again: Ctrl+L reaches the browser.
        await sleep(1000);
        const keys = sendKeys('Downloads test', ['^l']);
        check(/after: Edit/.test(keys), 'shortcuts work in a resumed tab (Ctrl+L)');
        sendKeys('Downloads test', ['{ESC}']);
      }
      tasks.close();
      // Closed again, so that keys go to the browser windows that follow.
      await devtoolsJson(port, `/json/close/${tasksTarget.id}`).catch(() => {});
      await sleep(1000);
    }

    // Command palette (003): Ctrl+K opens it; typing "bookmark manager"
    // and Enter runs that action (the bookmark manager in a tab).
    const palette = sendKeys('Downloads test', ['^k', 'bookmark manager'], {
      windowsOf: brouk.pid,
      thenKeys: ['{ENTER}'],
      window: ['BroukCommandPalette', 'Command palette'],
    });
    check(/window shown: True/.test(palette), 'Ctrl+K opens the command palette');
    let paletteRan;
    for (let i = 0; i < 20 && !paletteRan; i++) {
      await sleep(500);
      paletteRan = await devtoolsJson(port, '/json/list')
        .then((list) => list.find((t) => t.type === 'page' && t.url.startsWith('brouk://bookmarks')))
        .catch(() => undefined);
    }
    if (check(paletteRan !== undefined, 'the command palette runs the chosen action')) {
      sendKeys('Bookmarks', ['^w']);
    }

    // A new tab puts the keyboard in the address bar, also once its page
    // has loaded (as on macOS and in Chrome). In a new window (Ctrl+N), so
    // closing it leaves the other windows' active tabs as they were.
    const newTab = sendKeys('Downloads test', ['^n'], {
      windowsOf: brouk.pid,
      thenKeys: ['{END}'],
      afterKeys: ['^w'],
    });
    check(/then focus: Edit/.test(newTab), 'a new tab focuses the address bar');
  }

  // A crashed renderer shows the crash page (012); Brouk keeps running.
  await cdp.send('Page.navigate', { url: `${pagesBase}/crash-and-hang.html` });
  const crashPage = await waitForTitle(cdp, 'Crash and hang test');
  check(crashPage === 'Crash and hang test', `crash-and-hang.html loads ("${crashPage}")`);
  cdp.send('Page.crash').catch(() => {});
  await sleep(3000);
  desktopScreenshot('crash');
  cdp.close();
  check(brouk.exitCode === null, 'Brouk keeps running after a renderer crash');
  // The crashed tab, by its address: other windows' tabs are pages too.
  const after = await devtoolsJson(port, '/json/list')
    .then((list) => list.find((t) => t.type === 'page' && t.url.includes('/crash-and-hang.html')))
    .catch(() => null);
  if (check(after !== null && after !== undefined, 'the crashed tab is still there')) {
    const again = await Cdp.connect(after.webSocketDebuggerUrl);
    await again.send('Page.reload').catch(() => {});
    const title = await waitForTitle(again, 'Crash and hang test');
    check(title === 'Crash and hang test', 'the crashed tab loads again after a reload');
    again.close();
  }

  // Reopen closed tab (013): Ctrl+W, then Ctrl+Shift+T brings the page
  // back in a new tab (a new target).
  if (kWindows && after) {
    sendKeys('Crash and hang test', ['^w', '^+t']);
    let reopened;
    for (let i = 0; i < 20 && !reopened; i++) {
      await sleep(500);
      reopened = await devtoolsJson(port, '/json/list')
        .then((list) => list.find((t) => t.type === 'page' && t.url.includes('/crash-and-hang.html') && t.id !== after.id))
        .catch(() => undefined);
    }
    check(reopened !== undefined, 'Ctrl+Shift+T reopens the tab closed with Ctrl+W');
  }

  // History (013): Ctrl+H opens brouk://history in a tab, and the pages
  // visited so far are in it.
  if (kWindows) {
    sendKeys('Crash and hang test', ['^h']);
    let historyTarget;
    for (let i = 0; i < 20 && !historyTarget; i++) {
      await sleep(500);
      historyTarget = await devtoolsJson(port, '/json/list')
        .then((list) => list.find((t) => t.type === 'page' && t.url.startsWith('brouk://history')))
        .catch(() => undefined);
    }
    if (check(historyTarget !== undefined, 'Ctrl+H opens the history page')) {
      const historyPage = await Cdp.connect(historyTarget.webSocketDebuggerUrl);
      let answer = '';
      for (let i = 0; i < 20 && !answer.includes('/find-and-zoom.html'); i++) {
        await sleep(500);
        const { result } = await historyPage.send('Runtime.evaluate', {
          expression:
            "new Promise((ok) => window.cefQuery({ request: JSON.stringify({ type: 'history.sql', query: 'SELECT url FROM pages' }), " +
            "onSuccess: (r) => ok('ok ' + r), onFailure: (c, m) => ok('failed ' + m) }))",
          awaitPromise: true,
          returnByValue: true,
        });
        answer = result.value ?? '';
      }
      check(answer.includes('/find-and-zoom.html'), `the history has the visited test pages (${answer.slice(0, 200)})`);
      historyPage.close();

      // Address bar suggestions (003): typing lists the history page under
      // the field; Down selects it and Return loads it in place of the
      // history tab.
      const zoomPages = async () =>
        (await devtoolsJson(port, '/json/list').catch(() => [])).filter(
          (t) => t.type === 'page' && t.url.includes('/find-and-zoom.html'),
        ).length;
      const before = await zoomPages();
      // One PowerShell for typing, looking and choosing: another one
      // would deactivate Brouk and close the list.
      const shown = sendKeys('History', ['^l', 'find-and-zoom'], { windowsOf: brouk.pid, thenKeys: ['{DOWN}', '{ENTER}'] });
      check(/window shown: True/.test(shown), 'typing in the address bar shows suggestions');
      let after = before;
      for (let i = 0; i < 20 && after <= before; i++) {
        await sleep(500);
        after = await zoomPages();
      }
      check(after > before, `Down and Return load the suggested history page (${before} → ${after} tabs)`);
    }
  }

  // Passwords (008): brouk://passwords opens (here from a second start,
  // which hands its URL to the running Brouk); listing the passwords opens
  // each profile's database, whose key goes to Credential Manager.
  if (kWindows) {
    spawn(executable, ['brouk://passwords'], {
      env: { ...process.env, BROUK_DEV_DATA_DIR: dataDir },
      stdio: 'ignore',
    });
    let passwordsTarget;
    for (let i = 0; i < 30 && !passwordsTarget; i++) {
      await sleep(500);
      passwordsTarget = await devtoolsJson(port, '/json/list')
        .then((list) => list.find((t) => t.type === 'page' && t.url.startsWith('brouk://passwords')))
        .catch(() => undefined);
    }
    if (check(passwordsTarget !== undefined, 'brouk://passwords opens in a tab')) {
      const passwordsPage = await Cdp.connect(passwordsTarget.webSocketDebuggerUrl);
      const listPasswords = async () => {
        const { result } = await passwordsPage.send('Runtime.evaluate', {
          expression:
            "new Promise((ok) => window.cefQuery({ request: JSON.stringify({ type: 'passwords.list' }), " +
            "onSuccess: (r) => ok('ok ' + r), onFailure: (c, m) => ok('failed ' + m) }))",
          awaitPromise: true,
          returnByValue: true,
        });
        return result.value ?? '';
      };
      let answer = '';
      for (let i = 0; i < 20 && !answer.startsWith('ok'); i++) {
        await sleep(500);
        answer = await listPasswords();
      }
      check(answer === 'ok []', `the passwords page lists the (empty) passwords (${answer.slice(0, 200)})`);
      // cmdkey lists generic credentials as "Target: LegacyGeneric:target=<name>".
      const prefix = `Brouk Keys (${dataDir})/passwords-key-`;
      const keys = execFileSync('cmdkey', ['/list'], { encoding: 'utf8' })
        .split(/\r?\n/)
        .map((line) => line.replace(/^\s*Target:\s*(LegacyGeneric:target=)?/, '').trim())
        .filter((name) => name.startsWith(prefix));
      check(keys.length > 0, `the profiles' password keys are in Credential Manager (${keys.join(', ') || 'none'})`);
      await savePasswordFromLogin({ executable, port, pagesBase });
      passwordsPage.close();
      for (const key of keys) {
        execFileSync('cmdkey', [`/delete:${key}`], { stdio: 'ignore' });
      }
    }
  }

  // Autofill (009): an entry saved in Settings → Autofill is offered in the
  // drop-down of a form field; choosing it fills the form.
  if (kWindows) {
    await autofillFromEntry({ executable, port, pagesBase, pid: brouk.pid });
  }

  // Ad blocking (007): a rule in My filters blocks a request of the page
  // in front, and the shield counts it.
  if (kWindows) {
    await adblockMyFilter({ port, pagesBase, pid: brouk.pid });
  }

  // Quit as the user does (Brouk → Quit; on Windows closing the window),
  // aimed at this copy's pid only.
  const pagesBeforeQuit = await devtoolsJson(port, '/json/list').catch(() => []);
  log(`(pages before quitting: ${pagesBeforeQuit.map((t) => `${t.type} ${t.url}`).join(', ')})`);
  const quitStarted = Date.now();
  if (kWindows) {
    await closeWindowsOf(brouk);
  } else {
    execFileSync('osascript', [
      '-l',
      'JavaScript',
      '-e',
      `ObjC.import("AppKit"); $.NSRunningApplication.runningApplicationWithProcessIdentifier(${brouk.pid}).terminate`,
    ]);
  }
  while (brouk.exitCode === null && Date.now() - quitStarted < 10000) await sleep(200);
  if (!check(brouk.exitCode === 0, `Brouk quits cleanly within 10 seconds (exit code ${brouk.exitCode})`) && kWindows) {
    logWindowsOf(brouk.pid);
  }

  // Session restore (015): quitting saves the session; starting again
  // brings its windows back, with only their active tabs loaded.
  if (kWindows && brouk.exitCode === 0) {
    let session = '';
    try {
      session = readFileSync(join(dataDir, 'session.yaml'), 'utf8');
    } catch (error) {
      log(`(no session.yaml: ${error.message})`);
    }
    const urls = [...session.matchAll(/^\s*-?\s*url: "?([^"\n]+)"?$/gm)].map((m) => m[1]);
    const windows = (session.match(/^\s*-?\s*tabs:/gm) ?? []).length;
    log(`(session.yaml: ${windows} window(s), tabs ${urls.join(', ')})`);
    check(/^clean_exit: true$/m.test(session) && urls.length > 0, 'quitting saves the session with its tabs');
    // Which window closed last depends on taskkill's order: restore a
    // known session of two windows instead, active tabs in the middle.
    const tab = (name, extra = '') => `      - url: "${pagesBase}/${name}"\n        title: "${name}"\n        profile: "default"\n${extra}`;
    writeFileSync(
      join(dataDir, 'session.yaml'),
      'clean_exit: true\nwindows:\n' +
        `  - active: 1\n    tabs:\n${tab('audio.html')}${tab('dialogs.html')}` +
        `  - active: 0\n    tabs:\n${tab('print.html')}${tab('popups.html', '        pinned: true\n')}`,
    );
    const shown = [`${pagesBase}/dialogs.html`, `${pagesBase}/print.html`];
    const hidden = [`${pagesBase}/audio.html`, `${pagesBase}/popups.html`];
    const restartPort = port + 500;
    brouk = spawn(executable, ['--use-mock-keychain', `--remote-debugging-port=${restartPort}`], {
      env: { ...process.env, BROUK_DEV_DATA_DIR: dataDir },
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    brouk.stdout.on('data', (chunk) => appendFileSync(appLog, chunk));
    brouk.stderr.on('data', (chunk) => appendFileSync(appLog, chunk));
    log(`Started Brouk again (pid ${brouk.pid})`);
    let loaded = [];
    for (let i = 0; i < 40 && brouk.exitCode === null; i++) {
      await sleep(500);
      const list = await devtoolsJson(restartPort, '/json/list').catch(() => []);
      // Internal pages (the licences) load at once; about:blank is a new tab.
      loaded = list.filter((t) => t.type === 'page' && !/^(brouk|devtools):/.test(t.url)).map((t) => t.url);
      if (shown.every((url) => loaded.includes(url))) break;
    }
    log(`(pages after the restart: ${loaded.join(', ')})`);
    check(shown.every((url) => loaded.includes(url)), 'starting again brings both windows back with their active tabs');
    check(!hidden.some((url) => loaded.includes(url)), 'the background tabs load only when first shown');
    desktopScreenshot('restored');
    const quitAgain = Date.now();
    await closeWindowsOf(brouk);
    while (brouk.exitCode === null && Date.now() - quitAgain < 10000) await sleep(200);
    check(brouk.exitCode === 0, `the restored Brouk quits cleanly (exit code ${brouk.exitCode})`);
  }

  await sleep(2000); // ReportCrash writes its report a moment later.
  const newReports = crashReports().filter((name) => !reportsBefore.has(name));
  check(newReports.length === 0, `no new crash report of Brouk ${newReports.join(', ')}`);
  log(`Smoke tests took ${Math.round((Date.now() - started) / 1000)} s.`);
  finish(failures.length === 0 ? 0 : 1);
}

main().catch((error) => {
  log(`FAIL ${error.stack ?? error}`);
  finish(1);
});
