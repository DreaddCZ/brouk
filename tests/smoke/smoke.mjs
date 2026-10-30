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
import { mkdtempSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync, appendFileSync } from 'node:fs';
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

function finish(code) {
  if (code !== 0) desktopScreenshot('desktop');
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

  // A crashed renderer shows the crash page (012); Brouk keeps running.
  await cdp.send('Page.navigate', { url: `${pagesBase}/crash-and-hang.html` });
  const crashPage = await waitForTitle(cdp, 'Crash and hang test');
  check(crashPage === 'Crash and hang test', `crash-and-hang.html loads ("${crashPage}")`);
  cdp.send('Page.crash').catch(() => {});
  await sleep(3000);
  desktopScreenshot('crash');
  cdp.close();
  check(brouk.exitCode === null, 'Brouk keeps running after a renderer crash');
  const after = await pageTarget(port).catch(() => null);
  if (check(after !== null && after !== undefined, 'the crashed tab is still there')) {
    const again = await Cdp.connect(after.webSocketDebuggerUrl);
    await again.send('Page.reload').catch(() => {});
    const title = await waitForTitle(again, 'Crash and hang test');
    check(title === 'Crash and hang test', 'the crashed tab loads again after a reload');
    again.close();
  }

  // Quit as the user does (Brouk → Quit; on Windows closing the window),
  // aimed at this copy's pid only.
  const quitStarted = Date.now();
  if (kWindows) {
    // Without /F, taskkill asks the process's windows to close (WM_CLOSE).
    execFileSync('taskkill', ['/PID', String(brouk.pid)]);
  } else {
    execFileSync('osascript', [
      '-l',
      'JavaScript',
      '-e',
      `ObjC.import("AppKit"); $.NSRunningApplication.runningApplicationWithProcessIdentifier(${brouk.pid}).terminate`,
    ]);
  }
  while (brouk.exitCode === null && Date.now() - quitStarted < 10000) await sleep(200);
  check(brouk.exitCode === 0, `Brouk quits cleanly within 10 seconds (exit code ${brouk.exitCode})`);

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
