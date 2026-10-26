/**
 * The link routing part of the settings page (010): Brouk as the default
 * browser, the routing rules (pattern → profiles, in the picker's order)
 * and link wrappers (the built-in ones, and the user's own). Every change
 * is saved at once; rows still being filled in wait until they are
 * complete.
 */
import { cefTransport } from '../shared/bridge.js';
import { button, byId, element, errorText } from '../shared/dom.js';
import { LinksApi, type LinksState } from './links_api.js';
import {
  completeRules,
  completeWrappers,
  moveProfileUp,
  problemsByRow,
  profileName,
  profilesToAdd,
  rowsFromRules,
  rowsFromWrappers,
  type RuleRow,
  type WrapperRow,
} from './links_logic.js';

const api = new LinksApi(cefTransport());
let state: LinksState | null = null;
let rules: RuleRow[] = [];
let wrappers: WrapperRow[] = [];
let rowProblems = new Map<string, string>();

function showError(message: string | null): void {
  const box = byId<HTMLParagraphElement>('links-error');
  box.textContent = message ?? '';
  box.hidden = message === null;
}

function showSaved(text: string): void {
  byId('links-saved').textContent = text;
}

async function save(): Promise<void> {
  const completeR = completeRules(rules);
  const completeW = completeWrappers(wrappers);
  try {
    const problems = await api.save(completeR.entries, completeW.entries);
    rowProblems = problemsByRow(problems, completeR, completeW);
    showError(null);
    showSaved(problems.length === 0 ? 'Saved.' : 'Not saved: fix the marked entries.');
  } catch (error) {
    showError(errorText(error));
  }
  render();
}

function textInput(value: string, label: string, placeholder: string, onChange: (text: string) => void): HTMLInputElement {
  const input = element('input');
  input.type = 'text';
  input.value = value;
  input.placeholder = placeholder;
  input.spellcheck = false;
  input.autocomplete = 'off';
  input.setAttribute('aria-label', label);
  input.addEventListener('change', () => {
    onChange(input.value);
    void save();
  });
  return input;
}

function problemLine(key: string): HTMLElement | null {
  const message = rowProblems.get(key);
  if (message === undefined) return null;
  const line = element('p', { className: 'problem', text: message });
  line.setAttribute('role', 'alert');
  return line;
}

function ruleRow(row: RuleRow, index: number): HTMLElement {
  const box = element('div', { className: 'link-rule' });
  const pattern = textInput(row.pattern, 'Links matching', 'companya.atlassian.net, *.example.com, github.com/company/*', (text) => {
    row.pattern = text;
  });
  const chips = element('div', { className: 'link-profiles' });
  const profiles = state?.profiles ?? [];
  row.profiles.forEach((id, position) => {
    const chip = element('span', { className: 'link-chip' });
    chip.append(element('span', { text: `${position + 1}. ${profileName(id, profiles)}` }));
    if (position > 0) {
      const up = button('↑', () => {
        moveProfileUp(row, position);
        void save();
      });
      up.setAttribute('aria-label', `Offer ${profileName(id, profiles)} earlier`);
      chip.append(up);
    }
    const remove = button('×', () => {
      row.profiles.splice(position, 1);
      void save();
    });
    remove.setAttribute('aria-label', `Remove ${profileName(id, profiles)}`);
    chip.append(remove);
    chips.append(chip);
  });
  const add = element('select');
  add.setAttribute('aria-label', 'Add a profile to this rule');
  const prompt = element('option', { text: row.profiles.length === 0 ? 'Choose a profile…' : 'Add profile…' });
  prompt.value = '';
  add.append(prompt);
  for (const profile of profilesToAdd(row, profiles)) {
    const option = element('option', { text: profile.name });
    option.value = profile.id;
    add.append(option);
  }
  add.disabled = add.options.length === 1;
  add.addEventListener('change', () => {
    if (add.value === '') return;
    row.profiles.push(add.value);
    void save();
  });
  chips.append(add);
  const remove = button('Delete', () => {
    rules.splice(index, 1);
    void save();
  });
  remove.setAttribute('aria-label', `Delete the rule for ${row.pattern || 'a new pattern'}`);
  box.append(pattern, chips, remove);
  const problem = problemLine(`rule:${index}`);
  if (problem !== null) box.append(problem);
  return box;
}

function wrapperRow(row: WrapperRow, index: number): HTMLElement {
  const box = element('div', { className: 'link-wrapper' });
  const pattern = textInput(row.pattern, 'Wrapper address', 'links.example.com', (text) => {
    row.pattern = text;
  });
  const kind = element('select');
  kind.setAttribute('aria-label', 'Where the real address is');
  for (const [value, label] of [
    ['parameter', 'In a parameter'],
    ['opaque', 'Known only after loading'],
  ] as const) {
    const option = element('option', { text: label });
    option.value = value;
    option.selected = (value === 'opaque') === row.opaque;
    kind.append(option);
  }
  kind.addEventListener('change', () => {
    row.opaque = kind.value === 'opaque';
    void save();
  });
  const parameter = textInput(row.parameter, 'Parameter with the real address', 'url', (text) => {
    row.parameter = text;
  });
  // Kept in the grid (so rows line up), but not shown or reachable.
  parameter.style.visibility = row.opaque ? 'hidden' : 'visible';
  parameter.disabled = row.opaque;
  const remove = button('Delete', () => {
    wrappers.splice(index, 1);
    void save();
  });
  remove.setAttribute('aria-label', `Delete the wrapper ${row.pattern || 'being added'}`);
  box.append(pattern, kind, parameter, remove);
  const problem = problemLine(`wrapper:${index}`);
  if (problem !== null) box.append(problem);
  return box;
}

function render(): void {
  if (state === null) return;
  const defaultBox = byId<HTMLDivElement>('links-default');
  defaultBox.replaceChildren();
  if (state.isDefaultBrowser) {
    defaultBox.append(element('span', { text: 'Brouk is your default browser: links from other apps are routed.' }));
  } else {
    defaultBox.append(
      element('span', { text: 'Links from other apps reach Brouk only when it is the default browser.' }),
      button('Make Brouk the default browser', () => {
        api
          .makeDefault()
          .then(load)
          .catch((error: unknown) => showError(errorText(error)));
      }),
    );
  }

  const fileProblems = byId<HTMLUListElement>('links-file-problems');
  fileProblems.replaceChildren(...state.problems.map((p) => element('li', { text: p })));
  fileProblems.hidden = state.problems.length === 0;

  byId('links-rules').replaceChildren(...rules.map(ruleRow));
  byId('links-rules-empty').hidden = rules.length > 0;
  byId('links-builtins').replaceChildren(
    ...state.builtins.map((b) =>
      element('li', { text: `${b.name}: ${b.hosts}${b.opaque ? ' (followed before routing)' : ''}` }),
    ),
  );
  byId('links-wrappers').replaceChildren(...wrappers.map(wrapperRow));
}

async function load(): Promise<void> {
  try {
    state = await api.state();
    rules = rowsFromRules(state.rules);
    wrappers = rowsFromWrappers(state.wrappers);
    rowProblems = new Map();
    showError(null);
  } catch (error) {
    showError(errorText(error));
  }
  render();
}

/** Rows being filled in are kept; otherwise the files may have changed. */
function reloadIfSaved(): void {
  const unsaved =
    completeRules(rules).entries.length !== rules.length ||
    completeWrappers(wrappers).entries.length !== wrappers.length ||
    rowProblems.size > 0;
  if (!unsaved) void load();
}

export function startLinksPanel(): void {
  byId('links-add-rule').addEventListener('click', () => {
    rules.push({ pattern: '', profiles: [] });
    render();
    const inputs = byId('links-rules').querySelectorAll('input');
    inputs[inputs.length - 1]?.focus();
  });
  byId('links-add-wrapper').addEventListener('click', () => {
    wrappers.push({ pattern: '', opaque: false, parameter: '' });
    render();
  });
  window.addEventListener('focus', reloadIfSaved);
  void load();
}
