/**
 * The element picker (007): hovering highlights elements, a click picks
 * one. A small panel shows a CSS selector for it, which can be widened to
 * the parent, edited and previewed (the element is hidden while previewing).
 * "Create" hands the selector to the browser, which shows the rule for a
 * last check before saving it to My filters.
 *
 * Injected by the renderer when the user starts the picker; it takes its
 * native callback `__broukPicked` at once and removes it.
 */

function cssEscape(text: string): string {
  return typeof CSS !== 'undefined' && typeof CSS.escape === 'function' ? CSS.escape(text) : text.replace(/[^\w-]/g, '\\$&');
}

/** A selector for one element: its id, or tag with classes and position. */
function selectorFor(element: Element): string {
  if (element.id !== '' && document.querySelectorAll(`#${cssEscape(element.id)}`).length === 1) {
    return `#${cssEscape(element.id)}`;
  }
  const parts: string[] = [];
  let at: Element | null = element;
  while (at !== null && at !== document.documentElement && parts.length < 5) {
    let part = at.tagName.toLowerCase();
    const classes = Array.from(at.classList).filter((c) => !/\d{3,}/.test(c)).slice(0, 3);
    if (classes.length > 0) part += classes.map((c) => `.${cssEscape(c)}`).join('');
    parts.unshift(part);
    const candidate = parts.join(' > ');
    try {
      if (document.querySelectorAll(candidate).length === 1) return candidate;
    } catch {
      // Keep going up.
    }
    if (at.id !== '') {
      parts[0] = `#${cssEscape(at.id)}`;
      return parts.join(' > ');
    }
    at = at.parentElement;
  }
  return parts.join(' > ');
}

function startPicker(picked: (selector: string) => void): void {
  const host = document.createElement('brouk-picker');
  const shadow = host.attachShadow({ mode: 'closed' });
  shadow.innerHTML = `
    <style>
      .box { position: fixed; pointer-events: none; z-index: 2147483646;
        background: rgba(255, 90, 60, 0.25); outline: 2px solid rgb(255, 90, 60); }
      .panel { position: fixed; right: 16px; bottom: 16px; z-index: 2147483647;
        font: 13px -apple-system, sans-serif; background: #fff; color: #111;
        border-radius: 10px; box-shadow: 0 6px 24px rgba(0,0,0,.35); padding: 10px;
        width: 360px; display: flex; flex-direction: column; gap: 8px; }
      .panel input { font: 12px ui-monospace, Menlo, monospace; padding: 5px; }
      .row { display: flex; gap: 6px; justify-content: flex-end; }
      .hint { color: #666; font-size: 12px; }
      button { font: inherit; }
      @media (prefers-color-scheme: dark) { .panel { background: #2b2b2d; color: #eee; } }
    </style>
    <div class="box" hidden></div>
    <div class="panel">
      <div class="hint">Click the element to block. Esc cancels.</div>
      <input type="text" aria-label="Selector" spellcheck="false">
      <div class="row">
        <button type="button" data-act="parent" disabled>Parent</button>
        <button type="button" data-act="preview" disabled>Preview</button>
        <button type="button" data-act="cancel">Cancel</button>
        <button type="button" data-act="create" disabled>Create</button>
      </div>
    </div>`;
  const box = shadow.querySelector<HTMLDivElement>('.box');
  const panel = shadow.querySelector<HTMLDivElement>('.panel');
  const input = shadow.querySelector<HTMLInputElement>('input');
  if (box === null || panel === null || input === null) return;
  const buttons = (act: string): HTMLButtonElement | null => shadow.querySelector(`button[data-act="${act}"]`);
  let target: Element | null = null;
  let picking = true;
  let previewStyle: HTMLStyleElement | null = null;

  const highlight = (element: Element | null): void => {
    if (element === null) {
      box.hidden = true;
      return;
    }
    const rect = element.getBoundingClientRect();
    Object.assign(box.style, { left: `${rect.left}px`, top: `${rect.top}px`, width: `${rect.width}px`, height: `${rect.height}px` });
    box.hidden = false;
  };
  const choose = (element: Element): void => {
    target = element;
    input.value = selectorFor(element);
    highlight(element);
    for (const act of ['parent', 'preview', 'create']) {
      const b = buttons(act);
      if (b !== null) b.disabled = false;
    }
  };
  const stopPreview = (): void => {
    previewStyle?.remove();
    previewStyle = null;
  };
  const finish = (): void => {
    stopPreview();
    document.removeEventListener('mousemove', onMove, true);
    document.removeEventListener('click', onClick, true);
    document.removeEventListener('keydown', onKey, true);
    host.remove();
  };
  const onMove = (event: MouseEvent): void => {
    if (!picking) return;
    const element = document.elementFromPoint(event.clientX, event.clientY);
    if (element !== null && element !== host) highlight(element);
  };
  const onClick = (event: MouseEvent): void => {
    if (event.composedPath().includes(panel)) return;
    if (!picking) return;
    event.preventDefault();
    event.stopPropagation();
    const element = document.elementFromPoint(event.clientX, event.clientY);
    if (element !== null && element !== host) {
      picking = false;
      choose(element);
    }
  };
  const onKey = (event: KeyboardEvent): void => {
    if (event.key === 'Escape') {
      event.preventDefault();
      finish();
    }
  };
  panel.addEventListener('click', (event) => {
    const act = (event.target as HTMLElement).dataset['act'];
    if (act === 'cancel') {
      finish();
    } else if (act === 'parent' && target?.parentElement && target.parentElement !== document.body) {
      choose(target.parentElement);
    } else if (act === 'preview') {
      if (previewStyle !== null) {
        stopPreview();
      } else {
        previewStyle = document.createElement('style');
        previewStyle.textContent = `${input.value} { display: none !important; }`;
        document.documentElement.appendChild(previewStyle);
        box.hidden = true;
      }
    } else if (act === 'create' && input.value.trim() !== '') {
      const selector = input.value.trim();
      finish();
      picked(selector);
    }
  });
  document.addEventListener('mousemove', onMove, true);
  document.addEventListener('click', onClick, true);
  document.addEventListener('keydown', onKey, true);
  document.documentElement.appendChild(host);
}

(function () {
  const global = globalThis as unknown as Record<string, unknown>;
  const picked = global['__broukPicked'];
  delete global['__broukPicked'];
  if (typeof picked === 'function') {
    startPicker(picked as (selector: string) => void);
  }
})();
