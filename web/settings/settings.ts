/**
 * The settings page (004). Shows every setting the browser lists (all but
 * the advanced ones), grouped by section; settings for all profiles and for
 * one chosen profile are separate. Each change is saved at once; the
 * browser checks it and the page shows any problem next to the setting.
 */
import { cefTransport } from '../shared/bridge.js';
import { button, byId, element, errorText } from '../shared/dom.js';
import { SettingsApi, type SettingItem, type SettingsState } from './api.js';
import { isShown, parseSettingTarget, sections, shortcutFromKey } from './logic.js';

const api = new SettingsApi(cefTransport());
let state: SettingsState | null = null;
let profileId: string | null = null;
/** Problems from the last change, by "<scope>:<key>". */
const problems = new Map<string, string>();
const restartNeeded = new Set<string>();

function showError(message: string | null): void {
  const box = byId<HTMLParagraphElement>('error');
  box.textContent = message ?? '';
  box.hidden = message === null;
}

function showNotice(title: string | null, lines: readonly string[] = []): void {
  const box = byId<HTMLDivElement>('notice');
  box.replaceChildren();
  box.hidden = title === null;
  if (title === null) return;
  box.append(element('p', { text: title }));
  if (lines.length > 0) {
    const list = element('ul');
    list.append(...lines.map((line) => element('li', { text: line })));
    box.append(list);
  }
  box.append(button('OK', () => showNotice(null)));
}

async function change(
  scopeProfile: string | null,
  item: SettingItem,
  value: boolean | string | number,
): Promise<void> {
  const id = `${scopeProfile ?? 'global'}:${item.key}`;
  try {
    const restart = await api.set(scopeProfile, item.key, value);
    problems.delete(id);
    if (restart) restartNeeded.add(id);
  } catch (error) {
    problems.set(id, errorText(error));
  }
  await refresh();
}

function control(scopeProfile: string | null, item: SettingItem, inputId: string): HTMLElement {
  if (item.type === 'bool') {
    const box = element('input');
    box.type = 'checkbox';
    box.id = inputId;
    box.checked = item.value === true;
    box.addEventListener('change', () => void change(scopeProfile, item, box.checked));
    return box;
  }
  if (item.type === 'choice' || item.format === 'profileId') {
    const select = element('select');
    select.id = inputId;
    const options =
      item.format === 'profileId'
        ? (state?.profiles ?? []).map((p) => ({ value: p.id, label: p.name }))
        : item.choices;
    for (const option of options) {
      const node = element('option', { text: option.label });
      node.value = option.value;
      node.selected = option.value === item.value;
      select.append(node);
    }
    select.addEventListener('change', () => void change(scopeProfile, item, select.value));
    return select;
  }
  if (item.type === 'integer') {
    const number = element('input');
    number.type = 'number';
    number.id = inputId;
    number.min = String(item.min);
    number.max = String(item.max);
    number.step = '1';
    number.value = String(item.value);
    number.addEventListener('change', () => void change(scopeProfile, item, Number(number.value)));
    return number;
  }
  const input = element('input');
  input.type = 'text';
  input.id = inputId;
  input.value = typeof item.value === 'string' ? item.value : '';
  input.spellcheck = false;
  input.autocomplete = 'off';
  if (item.format === 'folder') input.placeholder = 'Downloads';
  if (item.format === 'urlTemplate') input.placeholder = 'https://example.com/search?q=%s';
  if (item.format === 'shortcut') {
    // Pressing a shortcut records it (003); plain keys edit the text, e.g.
    // to add a second shortcut after a comma or to type "none".
    input.placeholder = 'none';
    input.title = 'Press the keys to record a shortcut, or type e.g. cmd+shift+t';
    input.addEventListener('keydown', (event) => {
      const shortcut = shortcutFromKey(event);
      if (shortcut === null) return;
      event.preventDefault();
      input.value = shortcut;
      void change(scopeProfile, item, shortcut);
    });
  }
  // Saved when the field is left or Return is pressed.
  input.addEventListener('change', () => void change(scopeProfile, item, input.value.trim()));
  return input;
}

function settingRow(scopeProfile: string | null, item: SettingItem): HTMLDivElement {
  const row = element('div', { className: item.type === 'bool' ? 'setting toggle' : 'setting' });
  const inputId = `setting-${scopeProfile ?? 'global'}-${item.key}`;
  const title = element('label', { className: 'title', text: item.label });
  title.htmlFor = inputId;
  const id = `${scopeProfile ?? 'global'}:${item.key}`;
  if (item.restart && restartNeeded.has(id)) {
    title.append(element('span', { className: 'restart', text: 'Restart Brouk to apply' }));
  } else if (item.restart) {
    title.append(element('span', { className: 'restart', text: 'Needs a restart' }));
  }
  row.append(title, control(scopeProfile, item, inputId));
  if (item.description !== '') {
    row.append(element('p', { className: 'description', text: item.description }));
  }
  const problem = problems.get(id);
  if (problem !== undefined) {
    const text = element('p', { className: 'problem', text: problem });
    text.setAttribute('role', 'alert');
    row.append(text);
  }
  return row;
}

function renderItems(container: HTMLElement, scopeProfile: string | null, items: readonly SettingItem[]): void {
  container.replaceChildren(
    ...sections(items.filter((item) => isShown(item, items))).map((section) => {
      const group = element('section', { className: 'group' });
      group.setAttribute('aria-label', section.title);
      group.append(element('h3', { text: section.title }));
      group.append(...section.items.map((item) => settingRow(scopeProfile, item)));
      return group;
    }),
  );
}

function render(): void {
  if (state === null) return;
  const box = byId<HTMLDivElement>('problems');
  box.replaceChildren();
  box.hidden = state.problems.length === 0;
  if (state.problems.length > 0) {
    box.append(element('strong', { text: 'Some values in the settings files are invalid; their defaults are used:' }));
    const list = element('ul');
    list.append(...state.problems.map((p) => element('li', { text: p })));
    box.append(list);
  }
  renderItems(byId('global'), null, state.global);

  const picker = byId<HTMLSelectElement>('profile');
  picker.replaceChildren(
    ...state.profiles.map((p) => {
      const option = element('option', { text: p.name });
      option.value = p.id;
      option.selected = p.id === state?.profile.id;
      return option;
    }),
  );
  renderItems(byId('profile-settings'), state.profile.id, state.profile.items);
}

async function refresh(): Promise<void> {
  try {
    state = await api.state(profileId);
    profileId = state.profile.id;
    showError(null);
  } catch (error) {
    showError(errorText(error));
  }
  render();
}

/**
 * The command palette (003) opens the page at one setting:
 * brouk://settings/#<global or profile id>:<key>.
 */
async function showTarget(): Promise<void> {
  const target = parseSettingTarget(window.location.hash);
  if (target === null) return;
  // Cleared, so choosing the same setting again changes the hash again.
  history.replaceState(null, '', window.location.pathname);
  if (target.profile !== null && target.profile !== profileId) {
    profileId = target.profile;
    await refresh();
  }
  const input = document.getElementById(`setting-${target.profile ?? 'global'}-${target.key}`);
  const row = input?.closest('.setting');
  if (input === null || !(row instanceof HTMLElement)) return;
  row.scrollIntoView({ block: 'center' });
  input.focus();
  row.classList.add('highlight');
  setTimeout(() => row.classList.remove('highlight'), 1500);
}

async function fileAction(action: () => Promise<{ finished: boolean; problems: readonly string[] }>, done: string): Promise<void> {
  try {
    const result = await action();
    showError(null);
    if (result.finished) {
      showNotice(result.problems.length === 0 ? done : `${done} These were left out:`, result.problems);
    }
  } catch (error) {
    showError(errorText(error));
  }
  await refresh();
}

function start(): void {
  byId<HTMLSelectElement>('profile').addEventListener('change', (event) => {
    const select = event.currentTarget;
    if (select instanceof HTMLSelectElement) {
      profileId = select.value;
      void refresh();
    }
  });
  byId('open-folder').addEventListener('click', () => {
    api.openFolder().catch((error: unknown) => showError(errorText(error)));
  });
  byId('export').addEventListener('click', () => void fileAction(() => api.exportSettings(), 'Settings exported.'));
  byId('import').addEventListener('click', () => void fileAction(() => api.importSettings(), 'Settings imported.'));
  // Settings may change in the files or elsewhere: reload when the window
  // comes back to the front.
  window.addEventListener('focus', () => void refresh());
  window.addEventListener('hashchange', () => void showTarget());
  void refresh().then(showTarget);
}

start();
