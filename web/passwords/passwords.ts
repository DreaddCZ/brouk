/**
 * The passwords page (brouk://passwords, 008): the built-in passwords of
 * every profile with a profile filter and search, editing, the checkup,
 * the "never saved" sites, and CSV import and export. Revealing, copying,
 * editing and exporting passwords need Touch ID or the Mac's password; the
 * page then stays unlocked for 5 minutes.
 */
import { cefTransport } from '../shared/bridge.js';
import { button, byId, element, errorText } from '../shared/dom.js';
import { helpButton, wireHelpButtons } from '../shared/help.js';
import { LockedError, PasswordsApi, type Entry, type PageState, type Profile } from './api.js';
import { ago, checkupResult, displayHost, filterEntries, parseOthers } from './logic.js';
import { strengthScores } from './strength.js';

const api = new PasswordsApi(cefTransport());

let state: PageState = { profiles: [], currentProfile: '', unlockedFor: 0, breachCheck: true, bitwardenUnlocked: false };
let entries: Entry[] = [];
let unlockedUntil = 0;
/** Revealed secrets by "<profile>:<id>", hidden again when the page locks. */
const revealed = new Map<string, { password: string; note: string; code: string; secondsLeft: number }>();

const queryInput = byId<HTMLInputElement>('query');
const filter = byId<HTMLSelectElement>('profile-filter');
const lockButton = byId<HTMLButtonElement>('lock');
const errorBox = byId<HTMLParagraphElement>('error');
const toast = byId<HTMLDivElement>('toast');

function keyOf(entry: Entry): string {
  return `${entry.profile}:${entry.id}`;
}

function profileOf(id: string): Profile | undefined {
  return state.profiles.find((p) => p.id === id);
}

function builtinProfiles(): Profile[] {
  return state.profiles.filter((p) => !p.bitwarden);
}

function showError(error: unknown): void {
  errorBox.textContent = errorText(error);
  errorBox.hidden = false;
}

function clearError(): void {
  errorBox.hidden = true;
}

let toastTimer = 0;
function showToast(message: string, action?: { label: string; run: () => void }): void {
  toast.replaceChildren(element('span', { text: message }));
  if (action !== undefined) {
    toast.append(button(action.label, () => {
      toast.hidden = true;
      action.run();
    }));
  }
  toast.hidden = false;
  window.clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => {
    toast.hidden = true;
  }, 8000);
}

function unlocked(): boolean {
  return Date.now() < unlockedUntil;
}

function updateLock(): void {
  lockButton.textContent = unlocked() ? 'Lock' : 'Unlock';
  lockButton.title = unlocked()
    ? 'Passwords can be shown and changed; locks again after 5 minutes.'
    : 'Show and change passwords after Touch ID or your Mac password.';
}

/** Unlocks with Touch ID or the Mac's password if needed. */
async function ensureUnlocked(): Promise<boolean> {
  if (unlocked()) return true;
  const ok = await api.unlock();
  if (ok) {
    unlockedUntil = Date.now() + 5 * 60 * 1000;
    updateLock();
    window.setTimeout(onLocked, 5 * 60 * 1000 + 500);
  }
  return ok;
}

function onLocked(): void {
  if (unlocked()) return;
  revealed.clear();
  updateLock();
  renderList();
}

/** Runs an action that needs the page unlocked, unlocking first. */
async function withUnlock(action: () => Promise<void>): Promise<void> {
  clearError();
  try {
    if (!(await ensureUnlocked())) return;
    await action();
  } catch (error) {
    if (error instanceof LockedError) {
      unlockedUntil = 0;
      onLocked();
    }
    showError(error);
  }
}

function fillProfileSelect(select: HTMLSelectElement, profiles: readonly Profile[], selected: string, all: boolean): void {
  select.replaceChildren();
  if (all) select.append(new Option('All profiles', ''));
  for (const profile of profiles) {
    select.append(new Option(profile.name, profile.id, false, profile.id === selected));
  }
  if (!profiles.some((p) => p.id === selected)) select.value = all ? '' : (profiles[0]?.id ?? '');
}

function dot(profileId: string): HTMLSpanElement {
  const node = element('span', { className: 'dot' });
  const profile = profileOf(profileId);
  node.style.setProperty('--c', `var(--${profile?.color ?? 'grey'})`);
  node.title = profile?.name ?? '';
  return node;
}

// ---- Passwords list ----

function renderBitwarden(): void {
  const box = byId<HTMLDivElement>('bitwarden');
  box.replaceChildren();
  for (const profile of state.profiles) {
    if (!profile.bitwarden || (filter.value !== '' && filter.value !== profile.id)) continue;
    const row = element('div', { className: 'row' });
    const site = element('div', { className: 'site', text: profile.name });
    const user = element('div', {
      className: 'user',
      text: `Uses Bitwarden (${state.bitwardenUnlocked ? 'unlocked' : 'locked'}) — manage logins in Bitwarden`,
    });
    user.append(helpButton('passwords-bitwarden', 'Bitwarden'));
    const actions = element('div', { className: 'actions' });
    if (state.bitwardenUnlocked) {
      actions.append(
        button('Lock Bitwarden', () => {
          void api.lockBitwarden().then(async () => {
            state = await api.state();
            renderList();
          });
        }),
      );
    }
    row.append(dot(profile.id), site, user, actions);
    box.append(row);
  }
}

function renderList(): void {
  renderBitwarden();
  const list = byId<HTMLDivElement>('list');
  const empty = byId<HTMLParagraphElement>('empty');
  const shown = filterEntries(entries, filter.value, queryInput.value);
  list.replaceChildren();
  const now = Date.now() / 1000;
  for (const entry of shown) {
    const row = element('div', { className: 'row' });
    row.setAttribute('role', 'listitem');
    const site = element('div', { className: 'site', text: displayHost(entry.origin) });
    if (entry.others.length > 0) {
      site.append(element('small', { text: `also ${entry.others.map(displayHost).join(', ')}` }));
    }
    const user = element('div', { className: 'user', text: entry.username === '' ? '(no user name)' : entry.username });
    user.append(element('small', { text: `used ${ago(entry.lastUsed, now)}${entry.hasTotp ? ' · 2FA' : ''}` }));
    const actions = element('div', { className: 'actions' });
    const secret = revealed.get(keyOf(entry));
    actions.append(
      button('Copy user', () => void copy(entry, 'username')),
      button('Copy password', () => void withUnlock(() => copy(entry, 'password'))),
      button(secret === undefined ? 'Show' : 'Hide', () => void toggleReveal(entry)),
      button('Edit…', () => void withUnlock(() => openEditor(entry))),
      button('Delete', () => void removeEntry(entry), 'danger'),
    );
    row.append(dot(entry.profile), site, user, actions);
    if (secret !== undefined) {
      const line = element('div', { className: 'secret-line' });
      line.append(element('code', { text: secret.password }));
      if (secret.code !== '') {
        line.append(element('span', { text: `2FA ${secret.code} (${secret.secondsLeft} s)` }));
        line.append(button('Copy code', () => void withUnlock(() => copy(entry, 'code'))));
      }
      if (secret.note !== '') line.append(element('span', { className: 'note', text: secret.note }));
      row.append(line);
    }
    list.append(row);
  }
  empty.hidden = shown.length > 0;
  empty.textContent = entries.length === 0
    ? 'No saved passwords yet. Brouk offers to save them when you sign in to a site.'
    : 'No passwords match.';
}

async function copy(entry: Entry, field: 'username' | 'password' | 'code'): Promise<void> {
  clearError();
  try {
    await api.copy(entry.profile, entry.id, field);
    showToast(field === 'username' ? 'User name copied.' : field === 'password' ? 'Password copied.' : '2FA code copied.');
  } catch (error) {
    showError(error);
  }
}

async function toggleReveal(entry: Entry): Promise<void> {
  if (revealed.has(keyOf(entry))) {
    revealed.delete(keyOf(entry));
    renderList();
    return;
  }
  await withUnlock(async () => {
    const secrets = await api.reveal(entry.profile, entry.id);
    revealed.set(keyOf(entry), secrets);
    renderList();
  });
}

async function removeEntry(entry: Entry): Promise<void> {
  clearError();
  try {
    await api.remove(entry.profile, [entry.id]);
    revealed.delete(keyOf(entry));
    await reload();
    showToast(`Deleted the password for ${displayHost(entry.origin)}.`, {
      label: 'Undo',
      run: () => void api.undo().then(reload, showError),
    });
  } catch (error) {
    showError(error);
  }
}

// ---- Editor ----

const editor = byId<HTMLDivElement>('editor');
const editorForm = byId<HTMLFormElement>('editor-form');
const editorProfile = byId<HTMLSelectElement>('editor-profile');
const editorSite = byId<HTMLInputElement>('editor-site');
const editorOthers = byId<HTMLInputElement>('editor-others');
const editorUsername = byId<HTMLInputElement>('editor-username');
const editorPassword = byId<HTMLInputElement>('editor-password');
const editorTotp = byId<HTMLInputElement>('editor-totp');
const editorNote = byId<HTMLTextAreaElement>('editor-note');
const editorError = byId<HTMLParagraphElement>('editor-error');
let editing: Entry | null = null;

async function openEditor(entry: Entry | null): Promise<void> {
  editing = entry;
  editorError.hidden = true;
  byId('editor-title').textContent = entry === null ? 'Add password' : 'Edit password';
  const profiles = builtinProfiles();
  fillProfileSelect(editorProfile, profiles, entry?.profile ?? (filter.value || state.currentProfile), false);
  editorProfile.disabled = entry !== null;
  if (entry === null) {
    for (const input of [editorSite, editorOthers, editorUsername, editorPassword, editorTotp]) input.value = '';
    editorNote.value = '';
  } else {
    const secrets = await api.reveal(entry.profile, entry.id);
    editorSite.value = entry.origin;
    editorOthers.value = entry.others.join(', ');
    editorUsername.value = entry.username;
    editorPassword.value = secrets.password;
    editorTotp.value = secrets.totp;
    editorNote.value = secrets.note;
  }
  editor.hidden = false;
  (entry === null ? editorSite : editorPassword).focus();
}

function closeEditor(): void {
  editor.hidden = true;
  editorPassword.value = '';
  editorTotp.value = '';
  editorNote.value = '';
  editing = null;
}

editorForm.addEventListener('submit', (event) => {
  event.preventDefault();
  const id = editing?.id ?? 0;
  const save = async (): Promise<void> => {
    await api.save({
      profile: editorProfile.value,
      id,
      address: editorSite.value.trim(),
      others: parseOthers(editorOthers.value),
      username: editorUsername.value,
      password: editorPassword.value,
      note: editorNote.value,
      totp: editorTotp.value.trim(),
    });
    closeEditor();
    revealed.clear();
    await reload();
    showToast('Password saved.');
  };
  void (async () => {
    try {
      if (id !== 0 && !(await ensureUnlocked())) return;
      await save();
    } catch (error) {
      editorError.textContent = errorText(error);
      editorError.hidden = false;
    }
  })();
});
byId('editor-cancel').addEventListener('click', closeEditor);
editor.addEventListener('keydown', (event) => {
  if (event.key === 'Escape') closeEditor();
});

// ---- Profile chooser (import, export) ----

function chooseProfile(title: string, text: string, ok: string): Promise<string | null> {
  const modal = byId<HTMLDivElement>('chooser');
  const select = byId<HTMLSelectElement>('chooser-profile');
  byId('chooser-title').textContent = title;
  byId('chooser-text').textContent = text;
  byId('chooser-ok').textContent = ok;
  fillProfileSelect(select, builtinProfiles(), filter.value || state.currentProfile, false);
  modal.hidden = false;
  return new Promise((resolve) => {
    const form = byId<HTMLFormElement>('chooser-form');
    const done = (value: string | null): void => {
      modal.hidden = true;
      form.onsubmit = null;
      byId('chooser-cancel').onclick = null;
      resolve(value);
    };
    form.onsubmit = (event) => {
      event.preventDefault();
      done(select.value);
    };
    byId('chooser-cancel').onclick = () => done(null);
  });
}

byId('import').addEventListener('click', () => {
  void (async () => {
    clearError();
    const profile = await chooseProfile('Import passwords',
      'A CSV file exported from Chrome, Firefox, Safari or Bitwarden. Passwords already saved with the same site, ' +
      'user name and password are skipped.', 'Choose file…');
    if (profile === null) return;
    try {
      const summary = await api.importCsv(profile);
      if (summary === null) return;
      const body = byId('summary-body');
      body.replaceChildren(element('p', {
        text: `From ${summary.format}: ${summary.added} added, ${summary.skipped} already saved (skipped).`,
      }));
      if (summary.conflicts.length > 0) {
        body.append(element('p', {
          text: `${summary.conflicts.length} not imported: a different password is already saved for the same site ` +
            'and user name. Edit them by hand if the imported one is newer:',
        }));
        const items = element('ul');
        for (const c of summary.conflicts) items.append(element('li', { text: `${displayHost(c.origin)} — ${c.username}` }));
        body.append(items);
      }
      if (summary.problems.length > 0) {
        body.append(element('p', { text: 'Rows not imported:' }));
        const items = element('ul');
        for (const p of summary.problems) items.append(element('li', { text: p }));
        body.append(items);
      }
      byId('summary').hidden = false;
      await reload();
    } catch (error) {
      showError(error);
    }
  })();
});
byId('summary-ok').addEventListener('click', () => {
  byId('summary').hidden = true;
});

byId('export').addEventListener('click', () => {
  void (async () => {
    clearError();
    const profile = await chooseProfile('Export passwords',
      'The passwords are saved in a CSV file (Chrome\'s format) that is NOT encrypted: anyone who can open the file ' +
      'can read them. Delete it when you no longer need it.', 'Export…');
    if (profile === null) return;
    await withUnlock(async () => {
      if (await api.exportCsv(profile)) showToast('Passwords exported.');
    });
  })();
});

// ---- Checkup ----

async function runCheckup(): Promise<void> {
  const status = byId('checkup-status');
  const out = byId('checkup');
  await withUnlock(async () => {
    status.textContent = state.breachCheck ? 'Checking (including breaches)…' : 'Checking…';
    out.replaceChildren();
    const checkup = await api.checkup();
    const scores = await strengthScores(checkup.entries.map((e) => e.password));
    const result = checkupResult(checkup, scores);
    status.textContent = `${checkup.entries.length} passwords checked.`;
    const group = (title: string, count: number, explain: string, rows: Entry[][]): void => {
      const section = element('section', { className: 'checkup-group' });
      const heading = element('h2', { text: title });
      heading.append(element('span', { className: 'count', text: ` (${count})` }));
      section.append(heading);
      if (count === 0) {
        section.append(element('p', { className: 'good', text: explain }));
      }
      for (const set of rows) {
        const list = element('div', { className: 'list' });
        for (const entry of set) {
          const row = element('div', { className: 'row' });
          const actions = element('div', { className: 'actions' });
          actions.append(button('Edit…', () => void withUnlock(() => openEditor(entry))));
          const user = element('div', { className: 'user', text: entry.username || '(no user name)' });
          const breached = (entry as { breached?: number }).breached ?? 0;
          if (breached > 0) user.append(element('small', { text: `seen ${breached.toLocaleString()} times in breaches` }));
          row.append(dot(entry.profile), element('div', { className: 'site', text: displayHost(entry.origin) }), user,
            actions);
          list.append(row);
        }
        section.append(list);
      }
      out.append(section);
    };
    group('Breached passwords', result.breached.length,
      state.breachCheck ? 'None of your passwords appear in known breaches.' : 'The breach check is off in settings.',
      result.breached.length > 0 ? [result.breached.slice()] : []);
    group('Reused passwords', result.reused.length, 'Every password is used only once.',
      result.reused.map((g) => g.slice()));
    group('Weak passwords', result.weak.length, 'No weak passwords.', result.weak.length > 0 ? [result.weak.slice()] : []);
  });
  if (status.textContent?.startsWith('Checking')) status.textContent = '';
}
byId('run-checkup').addEventListener('click', () => void runCheckup());

// ---- Never saved ----

async function renderNever(): Promise<void> {
  const list = byId('never');
  const rows = (await api.neverSaved()).filter((n) => filter.value === '' || n.profile === filter.value);
  list.replaceChildren();
  for (const item of rows) {
    const row = element('div', { className: 'row' });
    row.setAttribute('role', 'listitem');
    const actions = element('div', { className: 'actions' });
    actions.append(button('Allow saving', () => {
      void api.allowSaving(item.profile, item.origin).then(renderNever, showError);
    }));
    row.append(dot(item.profile), element('div', { className: 'site', text: displayHost(item.origin) }),
      element('div', { className: 'user', text: profileOf(item.profile)?.name ?? '' }), actions);
    list.append(row);
  }
  byId('never-empty').hidden = rows.length > 0;
}

// ---- Tabs and start ----

function selectTab(name: 'list' | 'checkup' | 'never'): void {
  for (const tab of ['list', 'checkup', 'never'] as const) {
    byId(`tab-${tab}`).setAttribute('aria-selected', String(tab === name));
    byId(`view-${tab}`).hidden = tab !== name;
  }
  if (name === 'never') void renderNever().catch(showError);
}
byId('tab-list').addEventListener('click', () => selectTab('list'));
byId('tab-checkup').addEventListener('click', () => selectTab('checkup'));
byId('tab-never').addEventListener('click', () => selectTab('never'));

lockButton.addEventListener('click', () => {
  if (unlocked()) {
    unlockedUntil = 0;
    void api.lock();
    onLocked();
  } else {
    void withUnlock(async () => {});
  }
});
queryInput.addEventListener('input', renderList);
filter.addEventListener('change', () => {
  renderList();
  if (!byId('view-never').hidden) void renderNever().catch(showError);
});
byId('add').addEventListener('click', () => void openEditor(null).catch(showError));

async function reload(): Promise<void> {
  entries = await api.list();
  renderList();
}

async function start(): Promise<void> {
  try {
    state = await api.state();
    unlockedUntil = state.unlockedFor > 0 ? Date.now() + state.unlockedFor * 1000 : 0;
    updateLock();
    const initial = state.profiles.some((p) => p.id === state.currentProfile) ? state.currentProfile : '';
    fillProfileSelect(filter, state.profiles, initial, true);
    byId<HTMLButtonElement>('add').disabled = builtinProfiles().length === 0;
    await reload();
  } catch (error) {
    showError(error);
  }
}

void start();

wireHelpButtons();
