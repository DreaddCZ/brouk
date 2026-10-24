/**
 * Forms in web pages: login forms (008) and forms for autofill (009). Finds
 * them, tells the browser when one of their fields gets the focus and when
 * a form is submitted, and fills (and highlights) the values the browser
 * sends.
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
import { classifyAddressForm, type AutofillKind } from './address_rules.js';
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

  // ---- Autofill (009) ----
  interface AutofillTracked {
    readonly formId: number;
    readonly kind: AutofillKind | null;  // null: a custom field may fit.
    readonly hint: string;
  }
  type FillElement = HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement;
  const afTracked = new WeakMap<Element, AutofillTracked>();
  const fillable = new Map<number, WeakRef<FillElement>>();
  const isFillElement = (node: unknown): node is FillElement =>
    node instanceof HTMLInputElement || node instanceof HTMLSelectElement || node instanceof HTMLTextAreaElement;

  const describeAny = (element: FillElement): { info: FieldInfo; select: boolean } => {
    if (element instanceof HTMLInputElement) {
      return { info: describe(element), select: false };
    }
    const labels = Array.from(element.labels ?? []).map((l) => l.textContent ?? '');
    labels.push(element.getAttribute('aria-label') ?? '', element.title);
    const rect = element.getBoundingClientRect();
    return {
      info: {
        type: 'text',
        autocomplete: (element.getAttribute('autocomplete') ?? '').toLowerCase(),
        name: element.name.slice(0, 200),
        id: element.id.slice(0, 200),
        label: labels.join(' ').trim().slice(0, 200),
        placeholder: element instanceof HTMLTextAreaElement ? element.placeholder.slice(0, 200) : '',
        inputMode: '',
        maxLength: -1,
        visible: element.isConnected && !element.disabled && rect.width > 1 && rect.height > 1,
      },
      select: element instanceof HTMLSelectElement,
    };
  };

  let lastAfReport = '';
  const scanAutofill = (): void => {
    const byForm = new Map<Element | null, FillElement[]>();
    for (const element of Array.from(document.querySelectorAll('input, select, textarea'))) {
      if (!isFillElement(element) || (element instanceof HTMLInputElement &&
          (SKIPPED_TYPES.has(element.type) || element.type === 'password'))) {
        continue;
      }
      const list = byForm.get(element.form) ?? [];
      list.push(element);
      byForm.set(element.form, list);
    }
    const forms: { id: number; fields: { id: number; kind: string; hint: string }[] }[] = [];
    let any = false;
    for (const [form, elements] of byForm) {
      if (forms.length >= 50 || elements.length > 200) continue;
      const described = elements.map(describeAny);
      const kinds = classifyAddressForm(described.map((d) => d.info), described.map((d) => d.select));
      const formId = form !== null ? idOf(form) : idOf(document.documentElement);
      const fields: { id: number; kind: string; hint: string }[] = [];
      elements.forEach((element, i) => {
        const info = described[i]?.info;
        const kind = kinds[i] ?? null;
        if (info === undefined || !info.visible || tracked.has(element as HTMLInputElement) || fields.length >= 60) {
          afTracked.delete(element);
          return;
        }
        const hint = `${info.label} ${info.placeholder} ${info.name}`.trim().slice(0, 200);
        const id = idOf(element);
        fillable.set(id, new WeakRef(element));
        afTracked.set(element, { formId, kind, hint });
        fields.push({ id, kind: kind ?? '', hint });
        any = any || kind !== null;
      });
      if (fields.length > 0) forms.push({ id: formId, fields });
    }
    const report = any ? JSON.stringify(forms) : '';
    if (report !== lastAfReport) {
      lastAfReport = report;
      send('afforms', { forms: any ? forms : [] });
    }
  };

  const autofillSubmitted = (formId: number): void => {
    const values: Record<string, string> = {};
    let count = 0;
    for (const ref of fillable.values()) {
      const element = ref.deref();
      const info = element !== undefined ? afTracked.get(element) : undefined;
      if (element === undefined || info === undefined || info.formId !== formId || info.kind === null) continue;
      const value = element instanceof HTMLSelectElement
        ? (element.selectedOptions[0]?.text ?? '') : element.value;
      if (value.trim() !== '' && values[info.kind] === undefined) {
        values[info.kind] = value.slice(0, 1000);
        count++;
      }
    }
    if (count > 0) send('afsubmit', { form: formId, values });
  };

  let lastReport = '';
  const scan = (): void => {
    scanAutofill();
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
  const boxOf = (input: Element): { x: number; y: number; width: number; height: number } => {
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
    const element = event.target;
    if (!isFillElement(element)) {
      return;
    }
    if (element instanceof HTMLInputElement) {
      let info = tracked.get(element);
      if (info === undefined && !afTracked.has(element)) {
        scan();
        info = tracked.get(element);
      }
      if (info !== undefined) {
        send('focus', { form: info.formId, field: idOf(element), kind: info.kind, ...boxOf(element) });
        return;
      }
    }
    const af = afTracked.get(element);
    if (af !== undefined && !(element instanceof HTMLSelectElement)) {
      send('affocus', { form: af.formId, field: idOf(element), kind: af.kind ?? '', hint: af.hint,
        ...boxOf(element as HTMLInputElement) });
    }
  }, true);
  document.addEventListener('focusout', (event) => {
    if ((event.target instanceof HTMLInputElement && tracked.has(event.target)) ||
        (event.target instanceof Element && afTracked.has(event.target))) {
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
  const afFormOf = (target: EventTarget | null): number | undefined => {
    if (target instanceof HTMLFormElement) return ids.get(target);
    if (target instanceof Element) return afTracked.get(target)?.formId;
    return undefined;
  };
  document.addEventListener('submit', (event) => {
    const id = formOfTarget(event.target);
    if (id !== undefined) {
      submitted(id);
    }
    const af = afFormOf(event.target);
    if (af !== undefined) {
      autofillSubmitted(af);
    }
  }, true);
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') {
      const id = formOfTarget(event.target);
      if (id !== undefined) {
        submitted(id);
      }
      if (event.target instanceof HTMLInputElement) {
        const af = afFormOf(event.target);
        if (af !== undefined) autofillSubmitted(af);
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
      // Autofill only for buttons that submit (not "add to cart" and such).
      const submits = target instanceof HTMLButtonElement ? target.type === 'submit'
        : target instanceof HTMLInputElement;
      if (submits) autofillSubmitted(id);
    }
  }, true);

  // Fill messages from the browser: {"fields": {"<id>": "<value>"},
  // "highlight": true} (autofill highlights what it filled, as Chrome),
  // {"clear": [<id>, …]} and {"focused": "<value>"} (the focused field, from
  // the context menu).
  const valueSetter = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')?.set;
  const areaSetter = Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value')?.set;
  const highlighted = new WeakMap<HTMLElement, string>();
  const unhighlight = (element: HTMLElement): void => {
    const previous = highlighted.get(element);
    if (previous !== undefined) {
      element.style.backgroundColor = previous;
      highlighted.delete(element);
    }
  };
  const fold = (text: string): string => text.normalize('NFD').replace(/\p{M}/gu, '').toLowerCase().trim();
  const setValue = (element: FillElement, value: string, mark: boolean): void => {
    if (element instanceof HTMLSelectElement) {
      const wanted = fold(value);
      const option = Array.from(element.options).find((o) =>
        fold(o.value) === wanted || fold(o.text) === wanted) ??
        Array.from(element.options).find((o) => wanted !== '' && fold(o.text).startsWith(wanted));
      if (option === undefined) return;
      element.value = option.value;
    } else if (element instanceof HTMLTextAreaElement) {
      areaSetter?.call(element, value);
    } else {
      valueSetter?.call(element, value);
    }
    element.dispatchEvent(new Event('input', { bubbles: true }));
    element.dispatchEvent(new Event('change', { bubbles: true }));
    if (mark && !highlighted.has(element)) {
      highlighted.set(element, element.style.backgroundColor);
      element.style.backgroundColor = 'rgb(232, 240, 254)';
      // The highlight goes when the user changes the value, as in Chrome.
      element.addEventListener('input', (e) => {
        if (e.isTrusted) unhighlight(element);
      }, { once: true });
    }
  };
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
    const { fields, highlight, clear, focused } = message as {
      fields?: unknown; highlight?: unknown; clear?: unknown; focused?: unknown;
    };
    if (typeof focused === 'string') {
      const active = document.activeElement;
      if (isFillElement(active) && !(active instanceof HTMLSelectElement)) setValue(active, focused, false);
    }
    if (Array.isArray(clear)) {
      for (const key of clear as unknown[]) {
        const element = fillable.get(Number(key))?.deref();
        if (element !== undefined) {
          setValue(element, '', false);
          unhighlight(element);
        }
      }
    }
    if (typeof fields !== 'object' || fields === null) {
      return;
    }
    for (const [key, value] of Object.entries(fields)) {
      const element = elements.get(Number(key))?.deref() ?? fillable.get(Number(key))?.deref();
      if (element === undefined || typeof value !== 'string') {
        continue;
      }
      if (element instanceof HTMLInputElement && !visible(element)) {
        continue;
      }
      setValue(element, value, highlight === true);
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
