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
function sendKeys(title, keys) {
  const sends = keys
    .map((k) => `[System.Windows.Forms.SendKeys]::SendWait('${k.replaceAll("'", "''")}'); Start-Sleep -Milliseconds 300`)
    .join('; ');
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
  ].join('; ');
  // PowerShell can take long to start on a busy runner: one more try.
  for (let attempt = 1; attempt <= 2; attempt++) {
    try {
      const output = execFileSync('powershell', ['-NoProfile', '-Command', script], { encoding: 'utf8', timeout: 60000 });
      log(`(keys ${keys.join(' ')}: ${output.trim()})`);
      return;
    } catch (error) {
      log(`(keys not sent, attempt ${attempt}: ${error.message})`);
    }
  }
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

// The top-level windows a process still has, for the log.
function logWindowsOf(pid) {
  const script = [
    'Add-Type -AssemblyName UIAutomationClient',
    `$c = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, ${pid})`,
    '[System.Windows.Automation.AutomationElement]::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $c) | ForEach-Object { "$($_.Current.ClassName) [$($_.Current.Name)]" }',
  ].join('; ');
  try {
    const out = execFileSync('powershell', ['-NoProfile', '-Command', script], { timeout: 30000 });
    log(`(windows left: ${out.toString().trim().split(/\r?\n/).join(', ')})`);
  } catch (error) {
    log(`(no window list: ${error.message})`);
  }
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

async function main() {
  const executable = kWindows ? join(appPath, 'brouk.exe') : join(appPath, 'Contents/MacOS/brouk');
  const started = Date.now();
  const reportsBefore = new Set(crashReports());

  const printed = execFileSync(executable, ['--version'], { encoding: 'utf8' }).trim();
  check(printed === `Brouk ${version} (${commit})`, `--version prints "Brouk ${version} (${commit})" (got "${printed}")`);

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
    // The screenshot took the focus away; Brouk gives it back to the find
    // bar, where Escape closes it, then Ctrl+0 resets the zoom.
    sendKeys('Find and zoom test', ['{ESC}']);
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
