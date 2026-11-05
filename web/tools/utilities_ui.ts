/**
 * Building blocks of the utilities' panels (027): labelled fields, outputs
 * with Copy, option controls and remembered options.
 */
import { button, element } from '../shared/dom.js';
import type { Converted } from './util_common.js';

let nextId = 0;

/** A unique element id for labels. */
export function uid(prefix: string): string {
  nextId += 1;
  return `${prefix}-${nextId}`;
}

/** Copies text to the clipboard. */
export async function copyText(text: string): Promise<void> {
  try {
    await navigator.clipboard.writeText(text);
  } catch {
    // Internal pages may not have the clipboard permission: the old way.
    const area = element('textarea');
    area.value = text;
    document.body.append(area);
    area.select();
    document.execCommand('copy');
    area.remove();
  }
}

/** A labelled multi-line (or one-line) text field. */
export function textField(label: string, options: { lines?: number; placeholder?: string; readOnly?: boolean } = {}):
  { box: HTMLElement; input: HTMLTextAreaElement | HTMLInputElement } {
  const box = element('div', { className: 'util-field' });
  const id = uid('util');
  const title = element('label', { text: label });
  title.htmlFor = id;
  const lines = options.lines ?? 4;
  const input: HTMLTextAreaElement | HTMLInputElement = lines > 1 ? element('textarea') : element('input');
  if (input instanceof HTMLTextAreaElement) input.rows = lines;
  else input.type = 'text';
  input.id = id;
  input.spellcheck = false;
  input.autocomplete = 'off';
  if (options.placeholder !== undefined) input.placeholder = options.placeholder;
  if (options.readOnly === true) input.readOnly = true;
  box.append(title, input);
  return { box, input };
}

/** A read-only result with Copy, a note and a problem line. */
export class Output {
  readonly box: HTMLElement;
  readonly #field: HTMLTextAreaElement | HTMLInputElement;
  readonly #note = element('p', { className: 'util-note' });
  readonly #problem = element('p', { className: 'util-problem' });

  constructor(label: string, lines = 4) {
    const { box, input } = textField(label, { lines, readOnly: true });
    this.box = box;
    this.#field = input;
    const copy = button('Copy', () => void copyText(this.#field.value), 'small');
    copy.setAttribute('aria-label', `Copy ${label}`);
    // The label and Copy on one line.
    const head = element('div', { className: 'util-field-head' });
    const title = box.querySelector('label');
    if (title !== null) head.append(title);
    head.append(copy);
    box.prepend(head);
    this.#problem.setAttribute('role', 'alert');
    this.#note.hidden = true;
    this.#problem.hidden = true;
    box.append(this.#note, this.#problem);
  }

  get value(): string {
    return this.#field.value;
  }

  set(result: Converted): void {
    if (result.ok) {
      this.#field.value = result.text;
      this.#note.textContent = result.note ?? '';
      this.#note.hidden = result.note === undefined;
      this.#problem.hidden = true;
    } else {
      this.#field.value = '';
      this.#note.hidden = true;
      this.#problem.textContent = result.error;
      this.#problem.hidden = false;
    }
  }

  clear(): void {
    this.set({ ok: true, text: '' });
  }
}

/** A row of controls (options, buttons). */
export function row(...children: HTMLElement[]): HTMLElement {
  const box = element('div', { className: 'util-row' });
  box.append(...children);
  return box;
}

/** A labelled drop-down. */
export function select<T extends string>(label: string, choices: readonly (readonly [T, string])[], value: T,
  onChange: (value: T) => void): HTMLElement {
  const box = element('label', { className: 'util-option' });
  box.append(element('span', { text: label }));
  const node = element('select');
  for (const [key, text] of choices) {
    const option = element('option', { text });
    option.value = key;
    option.selected = key === value;
    node.append(option);
  }
  node.addEventListener('change', () => onChange(node.value as T));
  box.append(node);
  return box;
}

/** A labelled check box. */
export function checkbox(label: string, checked: boolean, onChange: (checked: boolean) => void): HTMLElement {
  const box = element('label', { className: 'util-option' });
  const input = element('input');
  input.type = 'checkbox';
  input.checked = checked;
  input.addEventListener('change', () => onChange(input.checked));
  box.append(input, element('span', { text: label }));
  return box;
}

/** Two choices as a segmented control (e.g. Encode | Decode). */
export function segmented<T extends string>(label: string, choices: readonly (readonly [T, string])[], value: T,
  onChange: (value: T) => void): HTMLElement {
  const box = element('div', { className: 'util-segmented' });
  box.setAttribute('role', 'radiogroup');
  box.setAttribute('aria-label', label);
  for (const [key, text] of choices) {
    const node = button(text, () => onChange(key));
    node.setAttribute('role', 'radio');
    node.setAttribute('aria-checked', String(key === value));
    box.append(node);
  }
  return box;
}

/**
 * The options each utility remembers (not its input: values may be
 * secrets). Kept in the tools page's local storage, for the user.
 */
export function loadOptions<T extends object>(id: string, defaults: T): T {
  try {
    const stored: unknown = JSON.parse(localStorage.getItem(`utilities.${id}`) ?? 'null');
    if (stored !== null && typeof stored === 'object') {
      const merged: Record<string, unknown> = { ...(defaults as Record<string, unknown>) };
      for (const [key, value] of Object.entries(stored)) {
        // Only known keys of the same type.
        if (key in defaults && typeof value === typeof (defaults as Record<string, unknown>)[key]) merged[key] = value;
      }
      return merged as T;
    }
  } catch {
    // Unreadable or no storage: the defaults.
  }
  return { ...defaults };
}

export function saveOptions(id: string, options: object): void {
  try {
    localStorage.setItem(`utilities.${id}`, JSON.stringify(options));
  } catch {
    // Not remembered then.
  }
}

/** Runs `fn` when the user stops typing for a moment. */
export function debounce(fn: () => void, ms = 150): () => void {
  let timer: ReturnType<typeof setTimeout> | undefined;
  return () => {
    clearTimeout(timer);
    timer = setTimeout(fn, ms);
  };
}

/** A place to drop a file or choose one; `onFile` gets its bytes. */
export function fileDrop(label: string, onFile: (file: File, bytes: Uint8Array<ArrayBuffer>) => void): HTMLElement {
  const box = element('div', { className: 'util-drop' });
  const input = element('input');
  input.type = 'file';
  input.hidden = true;
  const read = (file: File | undefined): void => {
    if (file === undefined) return;
    void file.arrayBuffer().then((buffer) => onFile(file, new Uint8Array(buffer)));
  };
  input.addEventListener('change', () => read(input.files?.[0]));
  box.append(element('span', { text: label }), button('Choose file…', () => input.click(), 'small'), input);
  box.addEventListener('dragover', (event) => {
    event.preventDefault();
    box.classList.add('over');
  });
  box.addEventListener('dragleave', () => box.classList.remove('over'));
  box.addEventListener('drop', (event) => {
    event.preventDefault();
    box.classList.remove('over');
    read(event.dataTransfer?.files[0]);
  });
  return box;
}
