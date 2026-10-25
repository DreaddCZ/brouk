/**
 * Login forms in web pages (008): finds them, tells the browser when a login
 * field gets the focus and when a login is submitted, and fills the values
 * the browser sends.
 *
 * Injected by the renderer into http(s) frames before the page's own
 * scripts, wrapped in a function so none of its names reach the page (the
 * build removes the import and export keywords and puts field_rules.ts in
 * front). The renderer gives it one native function, `__broukForms`, which
 * it takes and removes at once: ("start", receiver) registers the receiver
 * for fill messages, any other kind sends (kind, JSON) to the browser.
 *
 * The page can see and fake everything here; the browser treats every
 * message as an untrusted hint and decides itself what to fill.
 */
import { classifyForm } from './field_rules.js';
import type { FieldInfo, LoginKind } from './field_rules.js';

type Send = (kind: string, payload: unknown) => void;

interface Tracked {
  readonly formId: number;
  readonly kind: LoginKind;
}

const SKIPPED_TYPES = new Set(['hidden', 'submit', 'button', 'reset', 'image', 'checkbox', 'radio', 'file', 'range', 'color']);

(function start(): void {
  const host = globalThis as unknown as Record<string, unknown>;
  const native = host['__broukForms'];
  try {
    delete host['__broukForms'];
  } catch {
    // Not deletable: harmless, the page could call it anyway.
  }
  if (typeof native !== 'function') {
    return;
  }
  const call = native as (kind: string, payload: unknown) => void;
  const send: Send = (kind, payload) => {
    try {
      call(kind, JSON.stringify(payload));
    } catch {
      // The frame is going away.
    }
  };

  const ids = new WeakMap<Element, number>();
  const elements = new Map<number, WeakRef<HTMLInputElement>>();
  const tracked = new WeakMap<HTMLInputElement, Tracked>();
  let nextId = 1;
  const idOf = (element: Element): number => {
    let id = ids.get(element);
    if (id === undefined) {
      id = nextId++;
      ids.set(element, id);
    }
    return id;
  };

  const visible = (input: HTMLInputElement): boolean => {
    if (!input.isConnected || input.disabled) {
      return false;
    }
    const rect = input.getBoundingClientRect();
    if (rect.width < 2 || rect.height < 2) {
      return false;
    }
    const style = getComputedStyle(input);
    return style.visibility !== 'hidden' && style.display !== 'none' && Number(style.opacity) > 0.1;
  };

  const labelOf = (input: HTMLInputElement): string => {
    const parts: string[] = [];
    for (const label of Array.from(input.labels ?? [])) {
      parts.push(label.textContent ?? '');
    }
    parts.push(input.getAttribute('aria-label') ?? '', input.title);
    return parts.join(' ').trim().slice(0, 200);
  };

  const describe = (input: HTMLInputElement): FieldInfo => ({
    type: (input.getAttribute('type') ?? 'text').toLowerCase(),
    autocomplete: (input.getAttribute('autocomplete') ?? '').toLowerCase(),
    name: input.name.slice(0, 200),
    id: input.id.slice(0, 200),
    label: labelOf(input),
    placeholder: input.placeholder.slice(0, 200),
    inputMode: input.inputMode,
    maxLength: input.maxLength,
    visible: visible(input),
  });

  // Inputs grouped by form; inputs outside any form make one group per
  // document, as Chrome treats them.
  const groups = (): Map<Element | null, HTMLInputElement[]> => {
    const result = new Map<Element | null, HTMLInputElement[]>();
    for (const input of Array.from(document.querySelectorAll('input'))) {
      if (SKIPPED_TYPES.has(input.type)) {
        continue;
      }
      const key = input.form;
      const list = result.get(key) ?? [];
      list.push(input);
      result.set(key, list);
    }
    return result;
  };

  let lastReport = '';
  const scan = (): void => {
    const forms: { id: number; fields: { id: number; kind: LoginKind }[] }[] = [];
    let count = 0;
    for (const [form, inputs] of groups()) {
      if (count >= 50 || inputs.length > 200) {
        continue;
      }
      const kinds = classifyForm(inputs.map(describe));
      const fields: { id: number; kind: LoginKind }[] = [];
      const formId = form !== null ? idOf(form) : idOf(document.documentElement);
      inputs.forEach((input, i) => {
        const kind = kinds[i] ?? null;
        if (kind === null || fields.length >= 20) {
          tracked.delete(input);
          return;
        }
        const id = idOf(input);
        elements.set(id, new WeakRef(input));
        tracked.set(input, { formId, kind });
        fields.push({ id, kind });
      });
      if (fields.length > 0) {
        forms.push({ id: formId, fields });
        count++;
      }
    }
    const report = JSON.stringify(forms);
    if (report !== lastReport) {
      lastReport = report;
      send('forms', { forms });
    }
  };

  let scanTimer = 0;
  const scheduleScan = (): void => {
    if (scanTimer === 0) {
      scanTimer = window.setTimeout(() => {
        scanTimer = 0;
        scan();
      }, 250);
    }
  };

  // The field's box in the tab's viewport: frames add their offsets while
  // the parent is reachable (same origin).
  const boxOf = (input: HTMLInputElement): { x: number; y: number; width: number; height: number } => {
    const rect = input.getBoundingClientRect();
    let x = rect.left;
    let y = rect.top;
    let view: Window | null = window;
    try {
      while (view !== null && view.frameElement !== null && view.parent !== view) {
        const frameRect = view.frameElement.getBoundingClientRect();
        x += frameRect.left;
        y += frameRect.top;
        view = view.parent;
      }
    } catch {
      // A cross-origin parent: the box stays relative to this frame.
    }
    return { x, y, width: rect.width, height: rect.height };
  };

  document.addEventListener('focusin', (event) => {
    const input = event.target;
    if (!(input instanceof HTMLInputElement)) {
      return;
    }
    let info = tracked.get(input);
    if (info === undefined) {
      scan();
      info = tracked.get(input);
    }
    if (info !== undefined) {
      send('focus', { form: info.formId, field: idOf(input), kind: info.kind, ...boxOf(input) });
    }
  }, true);
  document.addEventListener('focusout', (event) => {
    if (event.target instanceof HTMLInputElement && tracked.has(event.target)) {
      send('blur', {});
    }
  }, true);

  // A submitted login: the form's values at the time of the submit.
  let lastSubmit = '';
  let watchTimer = 0;
  const submitted = (formId: number): void => {
    let username = '';
    let password = '';
    let newPassword = '';
    const passwordFields: HTMLInputElement[] = [];
    for (const ref of elements.values()) {
      const input = ref.deref();
      const info = input !== undefined ? tracked.get(input) : undefined;
      if (input === undefined || info === undefined || info.formId !== formId) {
        continue;
      }
      if (info.kind === 'username' && username === '') {
        username = input.value;
      } else if (info.kind === 'password' && password === '') {
        password = input.value;
        passwordFields.push(input);
      } else if (info.kind === 'new-password' && newPassword === '') {
        newPassword = input.value;
        passwordFields.push(input);
      }
    }
    if (username === '' && password === '' && newPassword === '') {
      return;
    }
    const key = `${formId}\n${username}\n${password}\n${newPassword}`;
    if (key === lastSubmit) {
      return;
    }
    lastSubmit = key;
    send('submit', { form: formId, username, password, new_password: newPassword });
    // Single-page logins: the form going away counts as success.
    window.clearInterval(watchTimer);
    let checks = 0;
    watchTimer = window.setInterval(() => {
      checks++;
      if (passwordFields.length > 0 && passwordFields.every((f) => !visible(f))) {
        window.clearInterval(watchTimer);
        send('gone', { form: formId });
      } else if (checks > 20) {
        window.clearInterval(watchTimer);
      }
    }, 500);
  };
  const formOfTarget = (target: EventTarget | null): number | undefined => {
    if (target instanceof HTMLInputElement) {
      return tracked.get(target)?.formId;
    }
    if (target instanceof HTMLFormElement) {
      return ids.get(target);
    }
    return undefined;
  };
  document.addEventListener('submit', (event) => {
    const id = formOfTarget(event.target);
    if (id !== undefined) {
      submitted(id);
    }
  }, true);
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      const id = formOfTarget(event.target);
      if (id !== undefined) {
        submitted(id);
      }
    }
  }, true);
  // Buttons that log in by script, without a form submit.
  document.addEventListener('click', (event) => {
    const target = event.target instanceof Element ? event.target.closest('button, [role="button"], input[type="submit"]') : null;
    if (target === null) {
      return;
    }
    const form = target.closest('form');
    const id = form !== null ? ids.get(form) : ids.get(document.documentElement);
    if (id !== undefined) {
      submitted(id);
    }
  }, true);

  // Fill messages from the browser: {"fields": {"<id>": "<value>"}}.
  const valueSetter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')?.set;
  const receive = (json: unknown): void => {
    if (typeof json !== 'string') {
      return;
    }
    let message: unknown;
    try {
      message = JSON.parse(json);
    } catch {
      return;
    }
    const fields = (message as { fields?: unknown }).fields;
    if (typeof fields !== 'object' || fields === null) {
      return;
    }
    for (const [key, value] of Object.entries(fields)) {
      const input = elements.get(Number(key))?.deref();
      if (input === undefined || typeof value !== 'string' || !visible(input)) {
        continue;
      }
      valueSetter?.call(input, value);
      input.dispatchEvent(new Event('input', { bubbles: true }));
      input.dispatchEvent(new Event('change', { bubbles: true }));
    }
  };
  call('start', receive);

  const observe = (): void => {
    scan();
    new MutationObserver((records) => {
      for (const record of records) {
        for (const node of Array.from(record.addedNodes)) {
          if (node instanceof HTMLInputElement || (node instanceof Element && node.querySelector('input') !== null)) {
            scheduleScan();
            return;
          }
        }
      }
    }).observe(document, { childList: true, subtree: true });
    // Fields that become visible later (dialogs, steps shown by script).
    document.addEventListener('transitionend', scheduleScan, true);
    document.addEventListener('animationend', scheduleScan, true);
  };
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', observe, { once: true });
  } else {
    observe();
  }
})();
