/**
 * The downloads page (brouk://downloads, 014): downloads of all profiles,
 * newest first, with a profile filter and search; the same actions as the
 * toolbar popup; "Clear all" keeps running downloads and every file.
 */
import { cefSubscriber, cefTransport } from '../shared/bridge.js';
import { button, byId, element, errorText } from '../shared/dom.js';
import { wireHelpButtons } from '../shared/help.js';
import { DownloadsApi, type Download, type Profile } from './api.js';
import { actionsFor, dayLabel, kActionLabels, searchDownloads, statusOf } from './logic.js';

const api = new DownloadsApi(cefTransport(), cefSubscriber());
let profiles: Profile[] = [];
let downloads: Download[] = [];

const filter = byId<HTMLSelectElement>('profile-filter');
const query = byId<HTMLInputElement>('query');

function showError(error: unknown): void {
  const box = byId('error');
  box.textContent = errorText(error);
  box.hidden = false;
}

function render(): void {
  const list = byId('list');
  list.replaceChildren();
  const shown = searchDownloads(downloads, query.value);
  const now = new Date();
  let day = '';
  let group: HTMLDivElement | null = null;
  for (const download of shown) {
    const label = dayLabel(download.start, now);
    if (label !== day || group === null) {
      day = label;
      list.append(element('h2', { className: 'day', text: label }));
      group = element('div', { className: 'list' });
      list.append(group);
    }
    const row = element('div', { className: 'row' });
    const dot = element('span', { className: 'dot' });
    const profile = profiles.find((p) => p.id === download.profile);
    dot.style.setProperty('--c', `var(--${profile?.color ?? 'grey'})`);
    dot.title = profile?.name ?? '';
    const main = element('div');
    const gone = download.state === 'complete' && !download.exists;
    main.append(element('div', { className: gone ? 'name gone' : 'name', text: download.name }));
    const meta = element('div', { className: download.state === 'failed' ? 'meta failed' : 'meta' });
    meta.textContent = [download.site, statusOf(download)].filter((t) => t !== '').join(' · ');
    meta.title = download.url;
    main.append(meta);
    if (download.state === 'in_progress' || download.state === 'paused') {
      const bar = element('progress');
      if (download.total > 0) {
        bar.max = download.total;
        bar.value = download.received;
      }
      main.append(bar);
    }
    const actions = element('div', { className: 'actions' });
    for (const action of actionsFor(download)) {
      actions.append(button(kActionLabels[action], () => {
        void api.action(download, action).then(reload, showError);
      }, action === 'delete' ? 'danger' : undefined));
    }
    row.append(dot, main, actions);
    group.append(row);
  }
  byId('empty').hidden = shown.length > 0;
}

async function reload(): Promise<void> {
  try {
    downloads = await api.list(filter.value);
    byId('error').hidden = true;
  } catch (error) {
    showError(error);
  }
  render();
}

async function start(): Promise<void> {
  try {
    const state = await api.state();
    profiles = state.profiles;
    filter.replaceChildren(new Option('All profiles', ''));
    for (const profile of profiles) filter.append(new Option(profile.name, profile.id));
  } catch (error) {
    showError(error);
  }
  filter.addEventListener('change', () => void reload());
  query.addEventListener('input', render);
  byId('clear').addEventListener('click', () => {
    void api.clear(filter.value).then(reload, showError);
  });
  // Progress and new downloads while the page is open (at most a few times
  // a second).
  let timer = 0;
  api.watch(() => {
    if (timer === 0) {
      timer = window.setTimeout(() => {
        timer = 0;
        void reload();
      }, 400);
    }
  });
  await reload();
}

void start();

wireHelpButtons();
