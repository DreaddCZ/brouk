/**
 * The ad blocking part of the settings page (007): filter lists (on/off,
 * add by URL, remove, Update now, each list's state) and My filters with
 * invalid lines marked. Per-profile choices (on/off, allowlist, kinds of
 * lists) are ordinary profile settings further down.
 */
import { cefTransport } from '../shared/bridge.js';
import { button, byId, element, errorText } from '../shared/dom.js';
import { AdblockApi, type AdblockState, type FilterProblem } from './adblock_api.js';
import { groupLists, isListUrl, listStatus } from './adblock_logic.js';

const api = new AdblockApi(cefTransport());
let state: AdblockState | null = null;
let pollTimer: number | undefined;
let checkTimer: number | undefined;

function showError(message: string | null): void {
  const box = byId<HTMLParagraphElement>('adblock-error');
  box.textContent = message ?? '';
  box.hidden = message === null;
}

async function run(action: () => Promise<void>): Promise<void> {
  try {
    showError(null);
    await action();
  } catch (error) {
    showError(errorText(error));
  }
}

function renderLists(): void {
  const box = byId<HTMLDivElement>('adblock-lists');
  box.replaceChildren();
  if (state === null) return;
  const now = Date.now() / 1000;
  for (const group of groupLists(state.categories, state.lists)) {
    box.append(element('h4', { text: group.category.label }));
    for (const list of group.lists) {
      const row = element('div', { className: 'adblock-list' });
      const toggle = element('input');
      toggle.type = 'checkbox';
      toggle.checked = list.enabled;
      toggle.id = `adblock-list-${list.id}`;
      toggle.addEventListener('change', () => void run(async () => {
        await api.setList(list.id, toggle.checked);
        await load();
      }));
      const label = element('label', { text: list.name });
      label.htmlFor = toggle.id;
      label.title = list.url;
      const status = element('span', { className: list.error === '' ? 'status' : 'status error-text', text: listStatus(list, now) });
      row.append(toggle, label, status);
      if (list.custom) {
        const remove = button('Remove', () => void run(async () => {
          await api.removeList(list.id);
          await load();
        }));
        remove.setAttribute('aria-label', `Remove ${list.url}`);
        row.append(remove);
      }
      box.append(row);
    }
  }
  const update = byId<HTMLButtonElement>('adblock-update');
  update.disabled = state.updating || !state.ready;
  update.textContent = state.updating ? 'Updating…' : 'Update now';
  const category = byId<HTMLSelectElement>('adblock-new-category');
  if (category.options.length === 0) {
    for (const item of state.categories) {
      const option = element('option', { text: item.label });
      option.value = item.id;
      option.selected = item.id === 'custom';
      category.append(option);
    }
  }
  // While lists download, the states change: look again soon.
  clearTimeout(pollTimer);
  const waiting = state.lists.some((l) => l.enabled && l.updated === 0 && l.error === '');
  if (state.updating || !state.ready || waiting) {
    pollTimer = window.setTimeout(() => void load(), 2000);
  }
}

async function load(): Promise<void> {
  await run(async () => {
    state = await api.state();
    renderLists();
  });
}

function showProblems(problems: readonly FilterProblem[]): void {
  const box = byId<HTMLUListElement>('adblock-filter-problems');
  box.replaceChildren(
    ...problems.map((p) => element('li', { text: `Line ${p.line}: ${p.error}` })),
  );
  box.hidden = problems.length === 0;
  byId<HTMLTextAreaElement>('adblock-filters').setAttribute('aria-invalid', String(problems.length > 0));
}

async function loadFilters(): Promise<void> {
  await run(async () => {
    const filters = await api.myFilters();
    byId<HTMLTextAreaElement>('adblock-filters').value = filters.text;
    showProblems(filters.problems);
  });
}

export function startAdblockPanel(): void {
  byId('adblock-update').addEventListener('click', () => void run(async () => {
    await api.update();
    await load();
  }));
  byId('adblock-add').addEventListener('submit', (event) => {
    event.preventDefault();
    const url = byId<HTMLInputElement>('adblock-new-url');
    if (!isListUrl(url.value)) {
      showError('Enter the list’s address (https://…).');
      return;
    }
    void run(async () => {
      await api.addList(byId<HTMLSelectElement>('adblock-new-category').value, url.value.trim());
      url.value = '';
      await load();
    });
  });
  const editor = byId<HTMLTextAreaElement>('adblock-filters');
  editor.addEventListener('input', () => {
    byId('adblock-filters-saved').textContent = '';
    clearTimeout(checkTimer);
    checkTimer = window.setTimeout(() => {
      api.checkFilters(editor.value).then(showProblems).catch(() => {});
    }, 400);
  });
  byId('adblock-filters-save').addEventListener('click', () => void run(async () => {
    showProblems(await api.saveMyFilters(editor.value));
    byId('adblock-filters-saved').textContent = 'Saved. Reload pages to apply.';
  }));
  void load();
  void loadFilters();
}
