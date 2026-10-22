/**
 * Small DOM helpers for internal pages. Text is always set with
 * textContent, never as HTML.
 */

export function byId<T extends HTMLElement>(id: string): T {
  const node = document.getElementById(id);
  if (node === null) {
    throw new Error(`Missing #${id}`);
  }
  return node as T;
}

export function element<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  options: { className?: string; text?: string } = {},
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (options.className !== undefined) node.className = options.className;
  if (options.text !== undefined) node.textContent = options.text;
  return node;
}

export function button(text: string, onClick: () => void, className?: string): HTMLButtonElement {
  const node = element('button', className === undefined ? { text } : { text, className });
  node.type = 'button';
  node.addEventListener('click', onClick);
  return node;
}

/** The message of a caught error, for showing it. */
export function errorText(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
