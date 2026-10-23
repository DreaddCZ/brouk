/** Request rules tool (006): rule texts and checks, kept free of the DOM. */
import { isRecord } from '../shared/bridge.js';

export type RuleAction =
  | 'set-request-header' | 'remove-request-header' | 'set-response-header' | 'remove-response-header'
  | 'redirect' | 'block';

export interface Rule {
  readonly pattern: string;
  readonly action: RuleAction;
  readonly header: string;
  readonly value: string;
  readonly enabled: boolean;
}

export const kActions: readonly [RuleAction, string][] = [
  ['set-request-header', 'Set request header'],
  ['remove-request-header', 'Remove request header'],
  ['set-response-header', 'Set response header'],
  ['remove-response-header', 'Remove response header'],
  ['redirect', 'Redirect to'],
  ['block', 'Block'],
];

export function needsHeader(action: RuleAction): boolean {
  return action !== 'redirect' && action !== 'block';
}

export function needsValue(action: RuleAction): boolean {
  return action === 'set-request-header' || action === 'set-response-header' || action === 'redirect';
}

export function parseRule(value: unknown): Rule | null {
  if (!isRecord(value) || typeof value['pattern'] !== 'string' || typeof value['action'] !== 'string') return null;
  const action = kActions.find(([a]) => a === value['action'])?.[0];
  if (action === undefined) return null;
  return {
    pattern: value['pattern'],
    action,
    header: typeof value['header'] === 'string' ? value['header'] : '',
    value: typeof value['value'] === 'string' ? value['value'] : '',
    enabled: value['enabled'] !== false,
  };
}

/** One line describing a rule: "api.example.com: Set request header X-Env: staging". */
export function describeRule(rule: Rule): string {
  const label = kActions.find(([a]) => a === rule.action)?.[1] ?? rule.action;
  switch (rule.action) {
    case 'block':
      return `${rule.pattern}: Block`;
    case 'redirect':
      return `${rule.pattern}: Redirect to ${rule.value}`;
    case 'remove-request-header':
    case 'remove-response-header':
      return `${rule.pattern}: ${label} ${rule.header}`;
    default:
      return `${rule.pattern}: ${label} ${rule.header}: ${rule.value}`;
  }
}

/** Quick feedback while typing; the browser checks again. */
export function ruleProblem(rule: Rule): string | null {
  if (!/^\s*(\*\.)?[A-Za-z0-9._-]+(:\d{1,5})?(\/\S*)?\s*$/.test(rule.pattern.replace(/^[a-z]+:\/\//i, ''))) {
    return 'Enter a pattern such as example.com, *.example.com or example.com/api/.';
  }
  if (needsHeader(rule.action) && !/^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/.test(rule.header)) {
    return 'Enter a header name.';
  }
  if (rule.action === 'redirect' && !/^https?:\/\//.test(rule.value)) {
    return 'Enter the http or https address to redirect to.';
  }
  if (/[\r\n]/.test(rule.value)) return 'The value must be a single line.';
  return null;
}
