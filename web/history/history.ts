/**
 * The history page (013): the history of all profiles, by site (one row per
 * exact host, expandable to its pages) or by day (single visits). Rows are
 * selected with their checkboxes (Shift-click for a range) and deleted
 * together; a title opens the page in its profile (⌘-click: in a new tab).
 */
import { cefTransport } from '../shared/bridge.js';
import { button, byId, element, errorText } from '../shared/dom.js';
import { HistoryApi, type DeleteItem, type Page, type Profile, type QueryProblem, type View } from './api.js';
import {
  addFilter,
  filterText,
  groupByDay,
  querySegments,
  hostLabel,
  lastVisitLabel,
  pathOf,
  sortSites,
  updateSelection,
  type Site,
  type SiteSort,
  type Visit,
} from './logic.js';

const api = new HistoryApi(cefTransport());
const kVisitsPerPage = 200;

let profiles: readonly Profile[] = [];
let view: View = 'site';
let profileFilter: string | null = null;
let sort: SiteSort = 'lastVisit';
let sites: Site[] = [];
const expanded = new Map<string, Page[] | 'loading'>();
let visits: Visit[] = [];
let visitsDone = false;
let selected = new Set<string>();
let anchor: string | null = null;
/** The search, and what of it was not understood. */
let query = '';
let problems: QueryProblem[] = [];
/** What each selection key deletes. */
const deletions = new Map<string, DeleteItem>();
/** Selection keys in the order shown (for Shift-click ranges). */
let order: string[] = [];

function showError(message: string | null): void {
  const box = byId<HTMLParagraphElement>('error');
  box.textContent = message ?? '';
  box.hidden = message === null;
}

function profileOf(id: string): Profile | undefined {
  return profiles.find((p) => p.id === id);
}

function siteKey(site: Site): string {
  return `site\u0000${site.profile}\u0000${site.host}`;
}

function dot(profile: string): HTMLSpanElement {
  const node = element('span', { className: 'dot' });
  const info = profileOf(profile);
  node.dataset['color'] = info?.color ?? 'grey';
  node.title = info?.name ?? profile;
  node.setAttribute('aria-label', `Profile ${info?.name ?? profile}`);
  return node;
}

function initial(host: string): HTMLSpanElement {
  const label = hostLabel(host);
  return element('span', { className: 'icon', text: (label[0] ?? '?').toUpperCase() });
}

function checkbox(key: string, label: string, item: DeleteItem): HTMLInputElement {
  deletions.set(key, item);
  order.push(key);
  const box = element('input');
  box.type = 'checkbox';
  box.checked = selected.has(key);
  box.setAttribute('aria-label', `Select ${label}`);
  box.addEventListener('click', (event) => {
    selected = updateSelection(selected, order, anchor, key, event.shiftKey);
    anchor = key;
    render();
  });
  return box;
}

/** A page of a selected site: deleted with the site, so checked and fixed. */
function includedCheckbox(label: string): HTMLInputElement {
  const box = element('input');
  box.type = 'checkbox';
  box.checked = true;
  box.disabled = true;
  box.title = 'Deleted with its site';
  box.setAttribute('aria-label', `${label}, deleted with its site`);
  return box;
}

function removeButton(label: string, items: readonly DeleteItem[]): HTMLButtonElement {
  const node = button('✕', () => void remove(items), 'remove');
  node.title = `Delete ${label}`;
  node.setAttribute('aria-label', `Delete ${label}`);
  return node;
}

function link(text: string, url: string, profile: string): HTMLAnchorElement {
  const node = element('a', { className: 'title', text: text === '' ? url : text });
  node.href = url;
  node.title = url;
  const open = (newTab: boolean): void => {
    api.open(profile, url, newTab).catch((error: unknown) => showError(errorText(error)));
  };
  node.addEventListener('click', (event) => {
    event.preventDefault();
    open(event.metaKey || event.ctrlKey);
  });
  node.addEventListener('auxclick', (event) => {
    if (event.button === 1) {
      event.preventDefault();
      open(true);
    }
  });
  return node;
}

function row(className: string, key: string, children: readonly Node[]): HTMLDivElement {
  const node = element('div', { className: selected.has(key) ? `${className} selected` : className });
  node.setAttribute('role', 'listitem');
  node.append(...children);
  return node;
}

function siteRows(site: Site, now: Date): HTMLElement[] {
  const key = siteKey(site);
  const label = hostLabel(site.host);
  const item: DeleteItem = { profile: site.profile, kind: 'site', value: site.host };
  const toggle = button(label, () => void toggleSite(site), 'title');
  toggle.setAttribute('aria-expanded', String(expanded.has(key)));
  const main = element('div', { className: 'main' });
  main.append(toggle);
  const meta = element('span', {
    className: 'meta',
    text: `${site.visits} ${site.visits === 1 ? 'visit' : 'visits'} · ${site.pages} ${site.pages === 1 ? 'page' : 'pages'}`,
  });
  const rows: HTMLElement[] = [
    row('row', key, [
      checkbox(key, label, item),
      dot(site.profile),
      initial(site.host),
      main,
      meta,
      element('span', { className: 'when', text: lastVisitLabel(site.lastVisit, now) }),
      removeButton(label, [item]),
    ]),
  ];
  const pages = expanded.get(key);
  // Deleting a site deletes all its pages: they show as selected, and are
  // not counted or deleted on their own.
  const siteSelected = selected.has(key);
  if (pages === 'loading') {
    rows.push(element('div', { className: 'row page', text: 'Loading…' }));
  } else if (pages !== undefined) {
    for (const page of pages) {
      const pageKey = `page\u0000${site.profile}\u0000${page.url}`;
      const pageItem: DeleteItem = { profile: site.profile, kind: 'page', value: page.url };
      const pageMain = element('div', { className: 'main' });
      pageMain.append(link(page.title, page.url, site.profile), element('span', { className: 'host', text: pathOf(page.url) }));
      rows.push(
        row(siteSelected ? 'row page selected' : 'row page', pageKey, [
          siteSelected
            ? includedCheckbox(page.title || page.url)
            : checkbox(pageKey, page.title || page.url, pageItem),
          element('span'),
          element('span'),
          pageMain,
          element('span', { className: 'meta', text: page.visits === 1 ? '1 visit' : `${page.visits} visits` }),
          element('span', { className: 'when', text: lastVisitLabel(page.lastVisit, now) }),
          removeButton(page.title || page.url, [pageItem]),
        ]),
      );
    }
  }
  return rows;
}

function visitRow(visit: Visit): HTMLElement {
  const key = `visit\u0000${visit.profile}\u0000${visit.id}`;
  const item: DeleteItem = { profile: visit.profile, kind: 'visit', id: visit.id };
  const main = element('div', { className: 'main' });
  main.append(link(visit.title, visit.url, visit.profile), element('span', { className: 'host', text: hostLabel(visit.host) }));
  const time = new Date(visit.time * 1000).toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit' });
  return row('row', key, [
    checkbox(key, visit.title || visit.url, item),
    dot(visit.profile),
    initial(visit.host),
    main,
    element('span'),
    element('span', { className: 'when', text: time }),
    removeButton(visit.title || visit.url, [item]),
  ]);
}

function render(): void {
  const now = new Date();
  deletions.clear();
  order = [];
  const list = byId<HTMLDivElement>('list');
  list.setAttribute('role', 'list');
  const nodes: HTMLElement[] = [];
  if (view === 'site') {
    for (const site of sortSites(sites, sort)) {
      nodes.push(...siteRows(site, now));
    }
  } else {
    for (const group of groupByDay(visits, now)) {
      nodes.push(element('h2', { className: 'day', text: group.label }));
      nodes.push(...group.visits.map(visitRow));
    }
  }
  list.replaceChildren(...nodes);
  const searching = query.trim() !== '';
  byId('empty').hidden = nodes.length > 0 || searching;
  byId('no-match').hidden = nodes.length > 0 || !searching;
  renderHint();
  byId('more').hidden = view !== 'day' || visitsDone;
  byId('view-site').setAttribute('aria-pressed', String(view === 'site'));
  byId('view-day').setAttribute('aria-pressed', String(view === 'day'));
  byId('sort-label').hidden = view !== 'site';
  // Selected rows that are no longer shown are not deleted.
  selected = new Set([...selected].filter((key) => deletions.has(key)));
  const count = selected.size;
  const label = byId('selection');
  label.hidden = count === 0;
  label.textContent = `${count} selected`;
  byId('delete').hidden = count === 0;
}

async function load(): Promise<void> {
  try {
    if (view === 'site') {
      const answer = await api.sites(profileFilter, query);
      sites = answer.sites;
      problems = answer.problems;
      // Expanded sites show their pages again.
      for (const [key, pages] of expanded) {
        const site = sites.find((s) => siteKey(s) === key);
        if (site === undefined) {
          expanded.delete(key);
        } else if (pages !== 'loading') {
          expanded.set(key, await api.pages(site.profile, site.host, query));
        }
      }
    } else {
      const answer = await api.visits(profileFilter, 0, kVisitsPerPage, query);
      visits = answer.visits;
      problems = answer.problems;
      visitsDone = visits.length < kVisitsPerPage;
    }
    showError(null);
  } catch (error) {
    showError(errorText(error));
  }
  render();
}

async function loadMore(): Promise<void> {
  const last = visits[visits.length - 1];
  if (last === undefined || visitsDone) return;
  try {
    const { visits: next } = await api.visits(profileFilter, last.time, kVisitsPerPage, query);
    visits = [...visits, ...next];
    visitsDone = next.length < kVisitsPerPage;
  } catch (error) {
    showError(errorText(error));
  }
  render();
}

async function toggleSite(site: Site): Promise<void> {
  const key = siteKey(site);
  if (expanded.has(key)) {
    expanded.delete(key);
    render();
    return;
  }
  expanded.set(key, 'loading');
  render();
  try {
    expanded.set(key, await api.pages(site.profile, site.host, query));
  } catch (error) {
    expanded.delete(key);
    showError(errorText(error));
  }
  render();
}

async function remove(items: readonly DeleteItem[]): Promise<void> {
  try {
    await api.remove(items);
    showError(null);
  } catch (error) {
    showError(errorText(error));
  }
  selected = new Set();
  await load();
}

function confirmDelete(): void {
  const items = [...selected].flatMap((key) => {
    const item = deletions.get(key);
    return item === undefined ? [] : [item];
  });
  if (items.length === 0) return;
  const box = byId<HTMLDivElement>('confirm');
  box.replaceChildren(
    element('span', { text: `Delete ${items.length} ${items.length === 1 ? 'item' : 'items'} from the history? This cannot be undone.` }),
    button('Delete', () => {
      box.hidden = true;
      void remove(items);
    }, 'danger'),
    button('Cancel', () => {
      box.hidden = true;
    }),
  );
  box.hidden = false;
}

/** Under the search box: what was not understood, underlined, and why. */
function renderHint(): void {
  const hint = byId<HTMLParagraphElement>('query-hint');
  const box = byId<HTMLInputElement>('query');
  const shown = problems.length > 0 && box.value === query;
  hint.hidden = !shown;
  box.setAttribute('aria-invalid', String(shown));
  if (!shown) {
    hint.replaceChildren();
    return;
  }
  const parts: Node[] = [];
  const reasons: string[] = [];
  for (const segment of querySegments(query, problems)) {
    if (segment.problem === undefined) {
      parts.push(document.createTextNode(segment.text));
    } else {
      parts.push(element('span', { className: 'bad', text: segment.text }));
      reasons.push(segment.problem);
    }
  }
  parts.push(element('span', { className: 'why', text: `\n${reasons.join('\n')} — the rest of the search still applies.` }));
  hint.replaceChildren(...parts);
}

let searchTimer: ReturnType<typeof setTimeout> | undefined;

function search(text: string, now = false): void {
  clearTimeout(searchTimer);
  const run = (): void => {
    query = text;
    selected = new Set();
    void load();
  };
  if (now) run();
  else searchTimer = setTimeout(run, 250);
}

/** A chip adds its filter to the search, as if typed. */
function applyFilter(filter: string): void {
  const box = byId<HTMLInputElement>('query');
  box.value = addFilter(box.value, filter);
  closeChipMenu();
  search(box.value, true);
  box.focus();
}

function closeChipMenu(): void {
  const menu = byId('chip-menu');
  menu.hidden = true;
  menu.replaceChildren();
}

function option(label: string, filter: string): HTMLButtonElement {
  return button(label, () => applyFilter(filter));
}

/** A small form: a text or date field and "Add". */
function inputForm(type: string, placeholder: string, toFilter: (value: string) => string): HTMLFormElement {
  const form = element('form');
  const input = element('input');
  input.type = type;
  input.placeholder = placeholder;
  input.setAttribute('aria-label', placeholder);
  const add = element('button', { text: 'Add' });
  add.type = 'submit';
  form.append(input, add);
  form.addEventListener('submit', (event) => {
    event.preventDefault();
    if (input.value.trim() !== '') applyFilter(toFilter(input.value.trim()));
  });
  queueMicrotask(() => input.focus());
  return form;
}

function openChipMenu(chip: HTMLElement): void {
  const menu = byId<HTMLDivElement>('chip-menu');
  const kind = chip.dataset['chip'];
  const items: Node[] = [];
  if (kind === 'site') {
    items.push(inputForm('text', 'example.com', (v) => filterText('site', v)));
  } else if (kind === 'date') {
    items.push(
      option('Today', 'on:today'),
      option('Yesterday', 'on:yesterday'),
      option('Last 7 days', 'after:7d'),
      option('Last 30 days', 'after:30d'),
      option('Older than 30 days', 'before:30d'),
      inputForm('date', 'On a day', (v) => filterText('on', v)),
    );
  } else if (kind === 'profile') {
    items.push(...profiles.map((p) => option(p.name, filterText('profile', p.name))));
  } else if (kind === 'visits') {
    items.push(option('More than 1 visit', 'visits:>1'), option('More than 3 visits', 'visits:>3'), option('More than 10 visits', 'visits:>10'), option('Visited once', 'visits:1'));
  } else if (kind === 'type') {
    items.push(option('Typed in the address bar', 'type:typed'), option('Opened from a link', 'type:link'), option('Opened from a bookmark', 'type:bookmark'), option('Reloaded', 'type:reload'));
  }
  menu.replaceChildren(...items);
  menu.setAttribute('aria-label', `Add a ${kind ?? ''} filter`);
  menu.style.left = `${chip.offsetLeft}px`;
  menu.hidden = false;
}

function setView(next: View): void {
  if (next === view) return;
  view = next;
  selected = new Set();
  api.setView(view).catch(() => undefined);
  void load();
}

async function start(): Promise<void> {
  try {
    const state = await api.state();
    profiles = state.profiles;
    view = state.view;
  } catch (error) {
    showError(errorText(error));
  }
  const picker = byId<HTMLSelectElement>('profile');
  const all = element('option', { text: 'All profiles' });
  all.value = '';
  picker.replaceChildren(all, ...profiles.map((p) => {
    const option = element('option', { text: p.name });
    option.value = p.id;
    return option;
  }));
  picker.addEventListener('change', () => {
    profileFilter = picker.value === '' ? null : picker.value;
    selected = new Set();
    expanded.clear();
    void load();
  });
  const sortPicker = byId<HTMLSelectElement>('sort');
  sortPicker.addEventListener('change', () => {
    const value = sortPicker.value;
    sort = value === 'visits' || value === 'host' ? value : 'lastVisit';
    render();
  });
  byId('view-site').addEventListener('click', () => setView('site'));
  byId('view-day').addEventListener('click', () => setView('day'));
  byId('delete').addEventListener('click', confirmDelete);
  byId('more').addEventListener('click', () => void loadMore());
  const box = byId<HTMLInputElement>('query');
  box.addEventListener('input', () => search(box.value));
  box.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') search(box.value, true);
  });
  for (const chip of document.querySelectorAll<HTMLElement>('.chip')) {
    chip.addEventListener('click', (event) => {
      event.stopPropagation();
      const menu = byId('chip-menu');
      if (!menu.hidden && menu.dataset['chip'] === chip.dataset['chip']) {
        closeChipMenu();
        return;
      }
      menu.dataset['chip'] = chip.dataset['chip'] ?? '';
      openChipMenu(chip);
    });
  }
  byId('chip-menu').addEventListener('click', (event) => event.stopPropagation());
  document.addEventListener('click', closeChipMenu);
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') closeChipMenu();
  });
  // The history changes while the page is open: reload when it is shown.
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') void load();
  });
  await load();
}

void start();
