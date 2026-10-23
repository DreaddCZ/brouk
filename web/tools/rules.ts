/**
 * Request rules tool (006): the profile's rules, which change requests of
 * its tabs that have dev tools on. Saved at once in the profile's
 * request-rules.yaml.
 */
import { button, element, errorText } from '../shared/dom.js';
import type { ToolsApi } from './api.js';
import {
  describeRule, kActions, needsHeader, needsValue, ruleLimitation, ruleProblem, type Rule, type RuleAction,
} from './rules_logic.js';

export class RulesTool {
  readonly #api: ToolsApi;
  readonly #root: HTMLElement;
  #rules: Rule[] = [];
  #paused = false;
  #profile = '';
  #problems: string[] = [];
  #error: string | null = null;
  /** Index being edited, -1 for a new rule, null for none. */
  #editing: number | null = null;
  #timer = 0;
  /** The last state shown, to re-render only when it changed. */
  #shown = '';

  constructor(api: ToolsApi, root: HTMLElement) {
    this.#api = api;
    this.#root = root;
  }

  get busy(): boolean {
    return this.#editing !== null;
  }

  /** Shows the rules and follows changes made elsewhere (the file, another window). */
  start(): void {
    void this.refresh();
    window.clearInterval(this.#timer);
    this.#timer = window.setInterval(() => { if (!this.busy) void this.refresh(true); }, 2000);
  }

  stop(): void {
    window.clearInterval(this.#timer);
    this.#timer = 0;
  }

  async refresh(onlyIfChanged = false): Promise<void> {
    try {
      const state = await this.#api.rules();
      const text = JSON.stringify(state);
      if (onlyIfChanged && text === this.#shown) return;
      this.#shown = text;
      this.#rules = state.rules;
      this.#paused = state.paused;
      this.#profile = state.profile;
      this.#problems = state.problems;
      this.#error = null;
    } catch (error) {
      this.#error = errorText(error);
    }
    this.render();
  }

  async #save(rules: Rule[], paused: boolean): Promise<void> {
    try {
      await this.#api.setRules(paused, rules);
      this.#editing = null;
      this.#error = null;
    } catch (error) {
      this.#error = errorText(error);
      this.render();
      return;
    }
    await this.refresh();
  }

  render(): void {
    const parts: HTMLElement[] = [];
    const bar = element('div', { className: 'toolbar' });
    bar.append(element('span', { className: 'origin', text: `Request rules of the profile “${this.#profile}”` }));
    const pause = element('label');
    const box = element('input');
    box.type = 'checkbox';
    box.checked = this.#paused;
    box.addEventListener('change', () => void this.#save(this.#rules, box.checked));
    pause.append(box, ' Pause all');
    bar.append(pause, button('Add rule', () => { this.#editing = -1; this.render(); }, 'primary'));
    parts.push(bar);
    parts.push(element('p', { className: 'note', text:
      'Rules apply only to tabs of this profile with developer tools on. When a rule changed the current page, ' +
      'the developer tools button turns orange. Cookies sent by the browser cannot be changed by rules.' }));
    if (this.#error !== null) parts.push(element('p', { className: 'error', text: this.#error }));
    if (this.#problems.length > 0) {
      const box2 = element('div', { className: 'error' });
      box2.append(element('strong', { text: 'Rules in the file that were left out:' }));
      const list = element('ul');
      list.append(...this.#problems.map((p) => element('li', { text: p })));
      box2.append(list);
      parts.push(box2);
    }
    const table = element('table');
    const head = element('tr');
    head.append(element('th', { className: 'check', text: 'On' }), element('th', { text: 'Rule' }), element('th'));
    table.append(head);
    this.#rules.forEach((rule, index) => {
      if (this.#editing === index) {
        table.append(this.#form(rule, index));
        return;
      }
      const row = element('tr');
      const on = element('input');
      on.type = 'checkbox';
      on.checked = rule.enabled;
      on.setAttribute('aria-label', `Rule ${index + 1} on`);
      on.addEventListener('change', () => void this.#save(
        this.#rules.map((r, i) => (i === index ? { ...r, enabled: on.checked } : r)), this.#paused));
      const onCell = element('td', { className: 'check' });
      onCell.append(on);
      const text = element('td', { text: describeRule(rule) });
      text.title = describeRule(rule);
      if (!rule.enabled || this.#paused) text.classList.add('flags');
      const limitation = ruleLimitation(rule);
      if (limitation !== null) text.append(element('div', { className: 'error-text', text: limitation }));
      const actions = element('td', { className: 'actions' });
      actions.append(
        button('Edit', () => { this.#editing = index; this.render(); }, 'small'), ' ',
        button('Delete', () => void this.#save(this.#rules.filter((_, i) => i !== index), this.#paused), 'small danger'),
      );
      row.append(onCell, text, actions);
      table.append(row);
    });
    if (this.#editing === -1) {
      table.append(this.#form({ pattern: '', action: 'set-request-header', header: '', value: '', enabled: true }, -1));
    }
    parts.push(table);
    if (this.#rules.length === 0 && this.#editing === null) {
      parts.push(element('div', { className: 'empty', text: 'No rules yet.' }));
    }
    this.#root.replaceChildren(...parts);
  }

  #form(rule: Rule, index: number): HTMLTableRowElement {
    const row = element('tr');
    const cell = element('td');
    cell.colSpan = 3;
    const form = element('form', { className: 'rule-form' });
    const input = (value: string, label: string): HTMLInputElement => {
      const node = element('input');
      node.type = 'text';
      node.value = value;
      node.placeholder = label;
      node.setAttribute('aria-label', label);
      node.spellcheck = false;
      return node;
    };
    const pattern = input(rule.pattern, 'URL pattern, e.g. api.example.com');
    const action = element('select');
    action.setAttribute('aria-label', 'Action');
    for (const [value, label] of kActions) {
      const option = element('option', { text: label });
      option.value = value;
      option.selected = value === rule.action;
      action.append(option);
    }
    const header = input(rule.header, 'Header name');
    const value = input(rule.value, 'Value');
    const problem = element('span', { className: 'error-text' });
    const update = (): void => {
      const chosen = action.value as RuleAction;
      header.hidden = !needsHeader(chosen);
      value.hidden = !needsValue(chosen);
      value.placeholder = chosen === 'redirect' ? 'Redirect to (https://…)' : 'Value';
    };
    action.addEventListener('change', update);
    update();
    const save = element('button', { className: 'small primary', text: 'Save' });
    save.type = 'submit';
    form.addEventListener('submit', (event) => {
      event.preventDefault();
      const chosen = action.value as RuleAction;
      const edited: Rule = {
        pattern: pattern.value.trim(), action: chosen,
        header: needsHeader(chosen) ? header.value.trim() : '',
        value: needsValue(chosen) ? value.value.trim() : '', enabled: rule.enabled,
      };
      const message = ruleProblem(edited);
      problem.textContent = message ?? '';
      if (message !== null) return;
      const rules = index === -1 ? [...this.#rules, edited] : this.#rules.map((r, i) => (i === index ? edited : r));
      void this.#save(rules, this.#paused);
    });
    form.append(pattern, action, header, value, save, ' ',
                button('Cancel', () => { this.#editing = null; this.render(); }, 'small'), problem);
    cell.append(form);
    row.append(cell);
    queueMicrotask(() => pattern.focus());
    return row;
  }
}
