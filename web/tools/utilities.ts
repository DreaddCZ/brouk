/**
 * Utilities tool (027): converters for testers (Base64, Unix time, URL and
 * HTML encoding, …) and the tab's environment. The list is on the left,
 * the chosen utility on the right; the fragment #utilities/<id> chooses one.
 * Inputs stay only while the dock is open.
 */
import { element } from '../shared/dom.js';
import type { ToolsApi } from './api.js';
import {
  base64Utility, hashUtility, hexTextUtility, htmlUtility, jsonUtility, numberBasesUtility, urlEncodingUtility,
  type Utility,
} from './utilities_converters.js';
import { environmentUtility } from './utilities_environment.js';
import {
  colorUtility, cronUtility, regexUtility, unixTimeUtility, urlParserUtility, uuidUtility,
} from './utilities_more.js';

/** The ids the browser may open directly (menu, command palette, "Decode with…"). */
export const kUtilityIds = [
  'base64', 'unix-time', 'url-encoding', 'url-parser', 'html', 'hash', 'uuid', 'json', 'number-bases', 'text-hex',
  'regex', 'cron', 'color', 'environment',
] as const;

export class UtilitiesTool {
  readonly #root: HTMLElement;
  readonly #utilities: Utility[];
  readonly #page = element('div', { className: 'utilities' });
  readonly #list = element('ul', { className: 'util-list' });
  readonly #filter = element('input');
  readonly #panel = element('section', { className: 'util-current' });
  #current: Utility;

  constructor(api: ToolsApi, root: HTMLElement) {
    this.#root = root;
    // Built once and kept, so inputs survive switching between tools.
    this.#utilities = [
      base64Utility(), unixTimeUtility(), urlEncodingUtility(), urlParserUtility(), htmlUtility(), hashUtility(),
      uuidUtility(), jsonUtility(), numberBasesUtility(), hexTextUtility(), regexUtility(), cronUtility(),
      colorUtility(), environmentUtility(api),
    ];
    this.#current = this.#utilities[0] as Utility;
    this.#filter.type = 'search';
    this.#filter.placeholder = 'Filter';
    this.#filter.setAttribute('aria-label', 'Filter utilities');
    this.#filter.addEventListener('input', () => this.#renderList());
    this.#filter.addEventListener('keydown', (event) => {
      // Return opens the first match.
      if (event.key !== 'Enter') return;
      const first = this.#visible()[0];
      if (first !== undefined) this.#choose(first, true);
    });
    const side = element('nav', { className: 'util-side' });
    side.setAttribute('aria-label', 'Utilities');
    side.append(this.#filter, this.#list);
    this.#page.append(side, this.#panel);
  }

  /** Shows the utility `id` (or the last one), with `input` from the page if given. */
  show(id: string | null, input: string | null): void {
    const chosen = this.#utilities.find((u) => u.id === id);
    if (chosen !== undefined) this.#current = chosen;
    if (input !== null) this.#current.setInput(input);
    this.#root.replaceChildren(this.#page);
    this.#renderList();
    this.#panel.replaceChildren(element('h2', { className: 'util-title', text: this.#current.title }), this.#current.element);
    document.title = `Utilities – ${this.#current.title}`;
  }

  /** Whether the user is typing (the page then does not refresh itself). */
  get busy(): boolean {
    return this.#page.contains(document.activeElement);
  }

  #visible(): Utility[] {
    const words = this.#filter.value.trim().toLowerCase().split(/\s+/).filter((w) => w !== '');
    return this.#utilities.filter((u) => {
      const text = `${u.title} ${u.keywords}`.toLowerCase();
      return words.every((w) => text.includes(w));
    });
  }

  #choose(utility: Utility, focus: boolean): void {
    // Through the fragment, so Back and the dock's own links agree.
    history.replaceState(null, '', `#utilities/${utility.id}`);
    this.show(utility.id, null);
    if (focus) utility.element.querySelector<HTMLElement>('textarea, input')?.focus();
  }

  #renderList(): void {
    this.#list.replaceChildren(...this.#visible().map((u) => {
      const item = element('li');
      const link = element('button', { text: u.title });
      link.type = 'button';
      if (u === this.#current) link.setAttribute('aria-current', 'page');
      link.addEventListener('click', () => this.#choose(u, false));
      item.append(link);
      return item;
    }));
  }
}
