/**
 * The profile manager page (005): list, create, rename, recolour, make
 * default, delete and open profiles. Built with DOM calls only; text from
 * the browser is set with textContent, never as HTML.
 */
import { cefTransport } from '../shared/bridge.js';
import { ProfilesApi, type ImportResult, type Profile, type ProfilesState } from './api.js';
import { colorLabel, nameProblem, tabsLabel } from './logic.js';

const api = new ProfilesApi(cefTransport());
let state: ProfilesState = { profiles: [], colors: [], showAtStartup: false };
/** The profile being edited, 'new' for the new-profile form, or null. */
let editing: string | null = null;

function byId<T extends HTMLElement>(id: string): T {
  const element = document.getElementById(id);
  if (element === null) {
    throw new Error(`Missing #${id}`);
  }
  return element as T;
}

function element<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  options: { className?: string; text?: string } = {},
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (options.className !== undefined) node.className = options.className;
  if (options.text !== undefined) node.textContent = options.text;
  return node;
}

function button(text: string, onClick: () => void, className?: string): HTMLButtonElement {
  const node = element('button', className === undefined ? { text } : { text, className });
  node.type = 'button';
  node.addEventListener('click', onClick);
  return node;
}

/** An information box (not an error), e.g. what an import left out. */
function showNotice(title: string | null, lines: readonly string[] = []): void {
  const box = byId<HTMLDivElement>('notice');
  box.replaceChildren();
  box.hidden = title === null;
  if (title === null) {
    return;
  }
  box.append(element('p', { className: 'notice-title', text: title }));
  if (lines.length > 0) {
    const list = element('ul');
    list.append(...lines.map((line) => element('li', { text: line })));
    box.append(list);
  }
  box.append(button('OK', () => showNotice(null)));
}

function showError(message: string | null): void {
  const box = byId<HTMLParagraphElement>('error');
  box.textContent = message ?? '';
  box.hidden = message === null;
}

/**
 * Runs a browser call, shows its error if it fails, then reloads the list.
 * Resolves with the call's result, or null if it failed.
 */
async function run<T>(action: () => Promise<T>): Promise<T | null> {
  try {
    const result = await action();
    showError(null);
    return result;
  } catch (error) {
    showError(error instanceof Error ? error.message : String(error));
    return null;
  } finally {
    await refresh();
  }
}

/** The form for a new profile (profile === null) or for editing one. */
function editForm(profile: Profile | null): HTMLFormElement {
  const form = element('form', { className: 'edit' });
  const nameId = `name-${profile?.id ?? 'new'}`;
  const field = element('label', { className: 'field', text: 'Name' });
  field.htmlFor = nameId;
  const input = element('input');
  input.type = 'text';
  input.id = nameId;
  input.value = profile?.name ?? '';
  input.placeholder = 'e.g. Work or Client A';
  input.autocomplete = 'off';
  input.maxLength = 64;
  field.append(input);
  const problem = element('p', { className: 'problem' });
  problem.setAttribute('aria-live', 'polite');

  const colors = element('fieldset', { className: 'colors' });
  colors.append(element('legend', { text: 'Colour' }));
  const used = new Set(state.profiles.map((p) => p.color));
  const initial = profile?.color ?? state.colors.find((c) => !used.has(c)) ?? state.colors[0] ?? '';
  for (const color of state.colors) {
    const swatch = element('label', { className: 'swatch' });
    swatch.dataset['color'] = color;
    swatch.title = colorLabel(color);
    const radio = element('input');
    radio.type = 'radio';
    radio.name = `color-${profile?.id ?? 'new'}`;
    radio.value = color;
    radio.checked = color === initial;
    radio.setAttribute('aria-label', colorLabel(color));
    swatch.append(radio, element('span'));
    colors.append(swatch);
  }

  const save = element('button', { className: 'primary', text: profile ? 'Save' : 'Create' });
  save.type = 'submit';
  const cancel = button('Cancel', () => {
    editing = null;
    render();
  });
  const buttons = element('div', { className: 'buttons' });
  buttons.append(cancel, save);
  form.append(field, problem, colors, buttons);

  const check = (): string | null => {
    const message = nameProblem(input.value, state.profiles, profile?.id ?? null);
    problem.textContent = message ?? '';
    return message;
  };
  input.addEventListener('input', () => void check());
  form.addEventListener('submit', (event) => {
    event.preventDefault();
    if (check() !== null) {
      input.focus();
      return;
    }
    const checked = form.querySelector<HTMLInputElement>('input[type=radio]:checked');
    const color = checked?.value ?? initial;
    const name = input.value.trim();
    void run(async () => {
      if (profile === null) {
        await api.create(name, color);
      } else {
        await api.update(profile.id, name, color);
      }
      editing = null;
    });
  });
  form.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
      editing = null;
      render();
    }
  });
  queueMicrotask(() => input.focus());
  return form;
}

function profileItem(profile: Profile): HTMLLIElement {
  const item = element('li');
  if (editing === profile.id) {
    item.append(editForm(profile));
    return item;
  }
  const card = element('div', { className: 'profile' });
  card.dataset['color'] = profile.color;
  const dot = element('span', { className: 'dot' });
  dot.setAttribute('aria-hidden', 'true');

  const who = element('div', { className: 'who' });
  const name = element('div', { className: 'name', text: profile.name });
  if (profile.isDefault) {
    name.append(element('span', { className: 'badge', text: 'Default' }));
  }
  who.append(name, element('div', { className: 'tabs', text: tabsLabel(profile.openTabs) }));

  const actions = element('div', { className: 'actions' });
  const open = button('Open', () => void run(() => api.open(profile.id)), 'primary');
  open.setAttribute('aria-label', `Open a window in ${profile.name}`);
  const edit = button('Edit', () => {
    editing = profile.id;
    render();
  });
  edit.setAttribute('aria-label', `Rename or recolour ${profile.name}`);
  const exportButton = button('Export…', () => void run(() => api.exportProfile(profile.id)));
  exportButton.setAttribute('aria-label', `Export ${profile.name} to a file`);
  actions.append(open, edit, exportButton);
  if (!profile.isDefault) {
    const makeDefault = button('Make default', () => void run(() => api.setDefault(profile.id)));
    makeDefault.setAttribute('aria-label', `Make ${profile.name} the default profile`);
    actions.append(makeDefault);
  }
  const remove = button('Delete', () => void run(() => api.remove(profile.id)), 'danger');
  remove.setAttribute('aria-label', `Delete ${profile.name}`);
  if (profile.isDefault) {
    remove.disabled = true;
    remove.title = 'The default profile cannot be deleted. Make another profile the default first.';
  }
  actions.append(remove);
  card.append(dot, who, actions);
  item.append(card);
  return item;
}

function render(): void {
  const list = byId<HTMLUListElement>('list');
  list.replaceChildren(...state.profiles.map(profileItem));
  const area = byId<HTMLDivElement>('new-area');
  if (editing === 'new') {
    area.replaceChildren(editForm(null));
  } else {
    const row = element('div', { className: 'new-buttons' });
    row.append(
      button('New profile', () => {
        editing = 'new';
        render();
      }),
      button('Import…', () => void importProfile()),
    );
    area.replaceChildren(row);
  }
  byId<HTMLInputElement>('startup').checked = state.showAtStartup;
}

async function importProfile(): Promise<void> {
  const imported: ImportResult | null = await run(() => api.importProfile());
  if (imported === null || imported.id === null) {
    return;  // Cancelled, or the error is shown.
  }
  const name = state.profiles.find((p) => p.id === imported.id)?.name ?? 'The profile';
  showNotice(
    imported.problems.length === 0
      ? `“${name}” was imported. Sign in to your sites again in it.`
      : `“${name}” was imported, except for:`,
    imported.problems,
  );
}

async function refresh(): Promise<void> {
  try {
    state = await api.state();
  } catch (error) {
    showError(error instanceof Error ? error.message : String(error));
  }
  render();
}

function start(): void {
  byId<HTMLInputElement>('startup').addEventListener('change', (event) => {
    const box = event.currentTarget;
    if (box instanceof HTMLInputElement) {
      void run(() => api.setShowAtStartup(box.checked));
    }
  });
  // Profiles may change elsewhere (menus, settings files): reload when the
  // window comes back to the front.
  window.addEventListener('focus', () => {
    if (editing === null) void refresh();
  });
  void refresh();
}

start();
