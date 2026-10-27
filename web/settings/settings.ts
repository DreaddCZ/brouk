/**
 * The settings page (004). Shows every setting the browser lists (all but
 * the advanced ones): the topics on the left, the chosen one on the right
 * with its settings for all profiles first and its per-profile settings
 * under a tab per profile. Each change is saved at once; the browser checks
 * it and the page shows any problem next to the setting.
 */
import { cefTransport } from '../shared/bridge.js';
import { button, byId, element, errorText } from '../shared/dom.js';
import { helpButton, settingsSectionAnchor, wireHelpButtons } from '../shared/help.js';
import { SettingsApi, type SettingItem, type SettingsState } from './api.js';
import { parseSettingTarget, sections, shortcutFromKey, topicOfSection, topics, type Topic } from './logic.js';
import { startAdblockPanel } from './adblock_panel.js';
import { startAutofillPanel } from './autofill_panel.js';
import { showProfileLinks, startLinksPanel } from './links_panel.js';

const api = new SettingsApi(cefTransport());
let state: SettingsState | null = null;
let profileId: string | null = null;
/** The topic shown on the right; the first one until the user picks one. */
let topicTitle: string | null = null;
/** The topic that only holds the settings files' buttons. */
const kFilesTopic = 'Settings files';
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
  if (item.format === 'urlPatterns') input.placeholder = 'login.example.com, *.example.org';
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

/** Where help explains a topic (016). */
function topicHelpAnchor(topic: string): string {
  if (topic === kFilesTopic) return 'settings-files';
  if (topic === 'Links') return 'link-routing';
  if (topic === 'Shortcuts') return 'keyboard-shortcuts';
  return settingsSectionAnchor(topic);
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
  if (item.format === 'urlPatterns') title.append(helpButton('url-patterns', 'URL patterns'));
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

function renderGroups(container: HTMLElement, scopeProfile: string | null, items: readonly SettingItem[]): void {
  container.replaceChildren(
    ...sections(items).map((section) => {
      const group = element('section', { className: 'group' });
      group.setAttribute('aria-label', section.title);
      group.append(...section.items.map((item) => settingRow(scopeProfile, item)));
      if (section.title === 'History' && scopeProfile === null) {
        const row = element('div', { className: 'setting' });
        row.append(
          element('span', { className: 'title', text: 'Browsing data' }),
          button('Clear browsing data…', () => {
            api.clearBrowsingData().catch((error: unknown) => showError(errorText(error)));
          }),
        );
        group.append(row);
      }
      return group;
    }),
  );
}

/** Shows the topic `title` on the right, from its top. */
function selectTopic(title: string): void {
  topicTitle = title;
  render();
  window.scrollTo({ top: 0 });
}

function renderSidebar(titles: readonly string[], current: string): void {
  byId('topics').replaceChildren(
    ...titles.map((title) => {
      const item = element('li');
      const node = button(title, () => selectTopic(title));
      if (title === current) node.setAttribute('aria-current', 'page');
      item.append(node);
      return item;
    }),
  );
}

/** A tab per profile; Left/Right move between them, as tabs do. */
function renderProfileTabs(current: string): void {
  if (state === null) return;
  const tabs = byId('profile-tabs');
  const profiles = state.profiles;
  tabs.replaceChildren(
    ...profiles.map((p, index) => {
      const tab = button(p.name, () => void chooseProfile(p.id));
      tab.id = `profile-tab-${p.id}`;
      tab.setAttribute('role', 'tab');
      tab.setAttribute('aria-controls', 'profile-settings');
      const selected = p.id === current;
      tab.setAttribute('aria-selected', String(selected));
      tab.tabIndex = selected ? 0 : -1;
      tab.addEventListener('keydown', (event) => {
        const step = event.key === 'ArrowRight' ? 1 : event.key === 'ArrowLeft' ? -1 : 0;
        if (step === 0) return;
        event.preventDefault();
        const next = profiles[(index + step + profiles.length) % profiles.length];
        if (next !== undefined) void chooseProfile(next.id, true);
      });
      return tab;
    }),
  );
  byId('profile-settings').setAttribute('aria-labelledby', `profile-tab-${current}`);
}

async function chooseProfile(id: string, focusTab = false): Promise<void> {
  profileId = id;
  await refresh();
  if (focusTab) document.getElementById(`profile-tab-${id}`)?.focus();
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

  const all: Topic[] = topics(state.global, state.profile.items);
  const titles = [...all.map((t) => t.title), kFilesTopic];
  const current = topicTitle !== null && titles.includes(topicTitle) ? topicTitle : (titles[0] ?? kFilesTopic);
  const topic = all.find((t) => t.title === current);
  renderSidebar(titles, current);
  byId('topic-title').replaceChildren(current, helpButton(topicHelpAnchor(current), current));
  renderGroups(byId('topic-global'), null, topic?.global ?? []);
  for (const panel of document.querySelectorAll<HTMLElement>('[data-topic]')) {
    panel.hidden = panel.dataset['topic'] !== current;
  }

  const perProfile = topic?.profile ?? [];
  const profileBox = byId('topic-profile');
  profileBox.hidden = perProfile.length === 0;
  // The heading only separates the two parts when both are there.
  byId('topic-profile-title').hidden = (topic?.global.length ?? 0) === 0;
  renderProfileTabs(state.profile.id);
  renderGroups(byId('profile-settings-groups'), state.profile.id, perProfile);
  showProfileLinks(current === 'Profiles' ? state.profile.id : null);
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
  if (target.key === 'links' || target.key === 'autofill') {
    // Settings → Links (010) and Autofill (009): the topic, not one setting.
    selectTopic(target.key === 'links' ? 'Links' : 'Autofill');
    return;
  }
  if (target.profile !== null && target.profile !== profileId) {
    profileId = target.profile;
    await refresh();
  }
  const items = target.profile === null ? state?.global : state?.profile.items;
  const item = items?.find((i) => i.key === target.key);
  if (item !== undefined) selectTopic(topicOfSection(item.section));
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
  startAdblockPanel();
  startAutofillPanel();
  startLinksPanel();
}

start();

wireHelpButtons();
