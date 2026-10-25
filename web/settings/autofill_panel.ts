/**
 * Settings → Autofill (009): the saved entries with usage, origin and
 * validation warnings, search and sort, editing, deleting several at once
 * with undo, duplicates and merging, custom fields, and JSON export and
 * import. The switches are ordinary settings in the "Autofill" section.
 */
import { cefTransport } from '../shared/bridge.js';
import { button, byId, element, errorText } from '../shared/dom.js';
import { AutofillApi, type AutofillEntryRow, type AutofillState, type CustomFieldRow } from './autofill_api.js';
import {
  addressLine, fullName, kFieldGroups, mergeConflicts, mergedValues, originLine, parseLabelWords, searchEntries,
  sortEntries, type EntrySort,
} from './autofill_logic.js';

const api = new AutofillApi(cefTransport());
let state: AutofillState | null = null;
const selected = new Set<number>();

function showError(message: string | null): void {
  const box = byId<HTMLParagraphElement>('autofill-error');
  box.textContent = message ?? '';
  box.hidden = message === null;
}

let toastTimer = 0;
function showToast(message: string, undo: boolean): void {
  const toast = byId<HTMLDivElement>('autofill-toast');
  toast.replaceChildren(element('span', { text: message }));
  if (undo) {
    toast.append(button('Undo', () => {
      toast.hidden = true;
      void run(async () => {
        await api.undo();
      });
    }));
  }
  toast.hidden = false;
  window.clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => {
    toast.hidden = true;
  }, 8000);
}

async function run(action: () => Promise<void>): Promise<void> {
  try {
    showError(null);
    await action();
  } catch (error) {
    showError(errorText(error));
  }
  await refresh();
}

function when(seconds: number): string {
  return seconds > 0 ? new Date(seconds * 1000).toLocaleDateString() : 'never';
}

function renderEntries(): void {
  const box = byId<HTMLDivElement>('autofill-entries');
  box.replaceChildren();
  if (state === null) return;
  const query = byId<HTMLInputElement>('autofill-search').value;
  const sort = byId<HTMLSelectElement>('autofill-sort').value as EntrySort;
  const shown = sortEntries(searchEntries(state.entries, query), sort);
  for (const entry of shown) {
    const row = element('div', { className: 'autofill-row' });
    const check = element('input');
    check.type = 'checkbox';
    check.checked = selected.has(entry.id);
    check.setAttribute('aria-label', `Select ${entry.label}`);
    check.addEventListener('change', () => {
      if (check.checked) selected.add(entry.id);
      else selected.delete(entry.id);
      renderSelection();
    });
    const main = element('div', { className: 'main' });
    const title = element('strong', { text: entry.label || '(no label)' });
    if (Object.keys(entry.problems).length > 0) {
      const warn = element('span', { className: 'warn', text: ' ⚠︎' });
      warn.title = Object.values(entry.problems).join('\n');
      warn.setAttribute('aria-label', `Invalid values: ${warn.title}`);
      title.append(warn);
    }
    main.append(title);
    const parts = [fullName(entry), entry.values['company'] ?? '', entry.values['email'] ?? '',
      entry.values['phone'] ?? '', addressLine(entry)].filter((p) => p !== '');
    main.append(element('div', { className: 'line', text: parts.join(' · ') }));
    const ids = ['ico', 'dic', 'vat_id', 'iban', 'bic'].map((f) => entry.values[f] ?? '').filter((v) => v !== '');
    if (ids.length > 0) main.append(element('div', { className: 'line', text: ids.join(' · ') }));
    main.append(element('div', {
      className: 'meta',
      text: `Last used ${when(entry.lastUsed)} · used ${entry.timesUsed}× · ${originLine(entry)}`,
    }));
    const actions = element('div', { className: 'actions' });
    actions.append(
      button('Edit…', () => openEditor(entry, false)),
      button('Duplicate', () => openEditor(entry, true)),
      button('Delete', () => {
        void run(async () => {
          await api.deleteEntries([entry.id]);
          showToast(`Deleted “${entry.label}”.`, true);
        });
      }, 'danger'),
    );
    row.append(check, main, actions);
    box.append(row);
  }
  byId('autofill-empty').hidden = shown.length > 0;
}

function renderSelection(): void {
  const remove = byId<HTMLButtonElement>('autofill-delete-selected');
  remove.hidden = selected.size === 0;
  remove.textContent = `Delete ${selected.size} selected`;
}

function renderCustom(): void {
  const box = byId<HTMLDivElement>('autofill-custom');
  box.replaceChildren();
  if (state === null) return;
  for (const field of state.custom) {
    box.append(customRow(field));
  }
}

/** A custom field, edited in place. */
function customRow(field: CustomFieldRow | null): HTMLElement {
  const row = element('form', { className: 'custom-row' });
  const name = element('input');
  name.placeholder = 'Name, e.g. Contract number';
  name.value = field?.name ?? '';
  name.setAttribute('aria-label', 'Name');
  const value = element('input');
  value.placeholder = 'Value';
  value.value = field?.value ?? '';
  value.setAttribute('aria-label', 'Value');
  const words = element('input');
  words.placeholder = 'Label words, e.g. číslo smlouvy, contract no';
  words.value = field?.labelWords.join(', ') ?? '';
  words.setAttribute('aria-label', 'Label words');
  const save = element('button', { text: field === null ? 'Add' : 'Save' });
  save.type = 'submit';
  row.append(name, value, words, save);
  if (field !== null) {
    row.append(element('span', { className: 'meta', text: `used ${when(field.lastUsed)}` }));
    row.append(button('Delete', () => {
      void run(async () => {
        await api.deleteCustom([field.id]);
        showToast(`Deleted “${field.name}”.`, true);
      });
    }, 'danger'));
  }
  row.addEventListener('submit', (event) => {
    event.preventDefault();
    void run(async () => {
      await api.saveCustom(field?.id ?? 0, name.value.trim(), value.value, parseLabelWords(words.value));
      if (field === null) row.remove();
    });
  });
  return row;
}

// ---- Editor ----

let editing: AutofillEntryRow | null = null;

function openEditor(entry: AutofillEntryRow | null, duplicate: boolean): void {
  editing = duplicate ? null : entry;
  byId('autofill-editor-title').textContent = entry === null ? 'Add entry' : duplicate ? 'New entry from a copy' : 'Edit entry';
  byId<HTMLInputElement>('autofill-label').value = entry === null ? '' : duplicate ? `${entry.label} (copy)` : entry.label;
  const groups = byId('autofill-fields');
  groups.replaceChildren();
  for (const group of kFieldGroups) {
    const set = element('fieldset');
    set.append(element('legend', { text: group.title }));
    for (const def of group.fields) {
      const label = element('label', { className: 'field' });
      label.append(element('span', { text: def.label }));
      const input = def.multiline === true ? element('textarea') : element('input');
      input.id = `autofill-field-${def.name}`;
      input.value = entry?.values[def.name] ?? '';
      if (input instanceof HTMLTextAreaElement) input.rows = 2;
      label.append(input);
      const problem = entry?.problems[def.name];
      if (problem !== undefined && !duplicate) {
        label.append(element('small', { className: 'warn', text: problem }));
      }
      set.append(label);
    }
    groups.append(set);
  }
  byId('autofill-origin').textContent = entry !== null && !duplicate ? `Origin: ${originLine(entry)}` : '';
  byId('autofill-editor-error').hidden = true;
  byId('autofill-editor').hidden = false;
  byId<HTMLInputElement>('autofill-label').focus();
}

function closeEditor(): void {
  byId('autofill-editor').hidden = true;
  editing = null;
}

function editorValues(): Record<string, string> {
  const values: Record<string, string> = {};
  for (const group of kFieldGroups) {
    for (const def of group.fields) {
      const input = document.getElementById(`autofill-field-${def.name}`) as HTMLInputElement | HTMLTextAreaElement | null;
      const value = input?.value.trim() ?? '';
      if (value !== '') values[def.name] = value;
    }
  }
  return values;
}

// ---- Duplicates ----

async function findDuplicates(): Promise<void> {
  const groups = await api.duplicates();
  const box = byId('autofill-duplicates');
  box.replaceChildren();
  box.hidden = false;
  if (groups.length === 0 || state === null) {
    box.append(element('p', { className: 'hint', text: 'No duplicates found.' }));
    return;
  }
  for (const ids of groups) {
    const entries = ids.map((id) => state?.entries.find((e) => e.id === id)).filter(
      (e): e is AutofillEntryRow => e !== undefined);
    const card = element('div', { className: 'duplicate-group' });
    card.append(element('strong', { text: entries.map((e) => e.label || '(no label)').join(' · ') }));
    card.append(button('Merge…', () => openMerge(entries)));
    box.append(card);
  }
}

function openMerge(entries: readonly AutofillEntryRow[]): void {
  const first = entries[0];
  if (first === undefined) return;
  const body = byId('autofill-merge-body');
  body.replaceChildren();
  const conflicts = mergeConflicts(entries);
  const labels = [...new Set(entries.map((e) => e.label).filter((l) => l !== ''))];
  const labelSelect = element('select');
  labelSelect.id = 'autofill-merge-label';
  for (const l of labels.length > 0 ? labels : ['']) labelSelect.append(new Option(l || '(no label)', l));
  const labelRow = element('label', { className: 'field' });
  labelRow.append(element('span', { text: 'Label' }), labelSelect);
  body.append(labelRow);
  if (conflicts.length === 0) {
    body.append(element('p', { className: 'hint', text: 'The entries do not disagree on any field.' }));
  }
  for (const conflict of conflicts) {
    const set = element('fieldset');
    set.append(element('legend', { text: kFieldGroups.flatMap((g) => g.fields).find((f) => f.name === conflict.field)?.label ?? conflict.field }));
    conflict.values.forEach((value, i) => {
      const option = element('label', { className: 'choice' });
      const radio = element('input');
      radio.type = 'radio';
      radio.name = `merge-${conflict.field}`;
      radio.value = value;
      radio.checked = i === 0;
      option.append(radio, element('span', { text: value }));
      set.append(option);
    });
    body.append(set);
  }
  const ok = byId<HTMLButtonElement>('autofill-merge-ok');
  ok.onclick = () => {
    const chosen: Record<string, string> = {};
    for (const conflict of conflicts) {
      const radio = document.querySelector<HTMLInputElement>(`input[name="merge-${conflict.field}"]:checked`);
      if (radio !== null) chosen[conflict.field] = radio.value;
    }
    byId('autofill-merge').hidden = true;
    void run(async () => {
      await api.merge(first.id, labelSelect.value, mergedValues(entries, chosen), entries.map((e) => e.id));
      byId('autofill-duplicates').hidden = true;
      showToast('Entries merged.', true);
    });
  };
  byId('autofill-merge').hidden = false;
}

async function refresh(): Promise<void> {
  try {
    state = await api.state();
    if (state.error !== '') showError(state.error);
  } catch (error) {
    showError(errorText(error));
  }
  for (const id of [...selected]) {
    if (!state?.entries.some((e) => e.id === id)) selected.delete(id);
  }
  renderEntries();
  renderSelection();
  renderCustom();
}

export function startAutofillPanel(): void {
  byId('autofill-search').addEventListener('input', renderEntries);
  byId('autofill-sort').addEventListener('change', renderEntries);
  byId('autofill-add').addEventListener('click', () => openEditor(null, false));
  byId('autofill-add-custom').addEventListener('click', () => {
    byId('autofill-custom').append(customRow(null));
  });
  byId('autofill-delete-selected').addEventListener('click', () => {
    const ids = [...selected];
    void run(async () => {
      await api.deleteEntries(ids);
      selected.clear();
      showToast(`Deleted ${ids.length} entries.`, true);
    });
  });
  byId('autofill-find-duplicates').addEventListener('click', () => {
    void findDuplicates().catch((error: unknown) => showError(errorText(error)));
  });
  byId('autofill-export').addEventListener('click', () => {
    void run(async () => {
      if (await api.exportJson()) showToast('Autofill data exported.', false);
    });
  });
  byId('autofill-import').addEventListener('click', () => {
    void run(async () => {
      const result = await api.importJson();
      if (result !== null) {
        showToast(`Imported: ${result.added} added, ${result.merged} merged, ${result.skipped} already saved; ` +
          `custom fields ${result.customAdded} added, ${result.customSkipped} already saved.`, false);
      }
    });
  });
  byId('autofill-editor-form').addEventListener('submit', (event) => {
    event.preventDefault();
    const id = editing?.id ?? 0;
    void (async () => {
      try {
        await api.saveEntry(id, byId<HTMLInputElement>('autofill-label').value.trim(), editorValues());
        closeEditor();
        await refresh();
      } catch (error) {
        const box = byId('autofill-editor-error');
        box.textContent = errorText(error);
        box.hidden = false;
      }
    })();
  });
  byId('autofill-editor-cancel').addEventListener('click', closeEditor);
  byId('autofill-merge-cancel').addEventListener('click', () => {
    byId('autofill-merge').hidden = true;
  });
  // Entries saved from a page's prompt while this page is open.
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') void refresh();
  });
  window.addEventListener('focus', () => void refresh());
  void refresh();
}
