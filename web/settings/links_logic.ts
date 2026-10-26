/**
 * Link routing settings (010) as the page edits them: rows the user is
 * still filling in stay on the page and are not saved until complete.
 */
import type { LinkProfile, LinkRule, LinkWrapper, SaveProblem } from './links_api.js';

export interface RuleRow {
  pattern: string;
  profiles: string[];
}

export interface WrapperRow {
  pattern: string;
  opaque: boolean;
  parameter: string;
}

/** What is sent to the browser, and which row each entry came from. */
export interface Complete<T> {
  readonly entries: T[];
  readonly rows: number[];
}

export function completeRules(rows: readonly RuleRow[]): Complete<LinkRule> {
  const entries: LinkRule[] = [];
  const indexes: number[] = [];
  rows.forEach((row, index) => {
    const pattern = row.pattern.trim();
    if (pattern !== '' && row.profiles.length > 0) {
      entries.push({ pattern, profiles: [...row.profiles] });
      indexes.push(index);
    }
  });
  return { entries, rows: indexes };
}

export function completeWrappers(rows: readonly WrapperRow[]): Complete<LinkWrapper> {
  const entries: LinkWrapper[] = [];
  const indexes: number[] = [];
  rows.forEach((row, index) => {
    const pattern = row.pattern.trim();
    const parameter = row.opaque ? '' : row.parameter.trim();
    if (pattern !== '' && (row.opaque || parameter !== '')) {
      entries.push({ pattern, parameter });
      indexes.push(index);
    }
  });
  return { entries, rows: indexes };
}

/** The browser's problems, by the row they belong to ("rule:2"). */
export function problemsByRow(
  problems: readonly SaveProblem[],
  rules: Complete<LinkRule>,
  wrappers: Complete<LinkWrapper>,
): Map<string, string> {
  const result = new Map<string, string>();
  for (const problem of problems) {
    const rows = problem.kind === 'rule' ? rules.rows : wrappers.rows;
    const row = rows[problem.index];
    if (row !== undefined) result.set(`${problem.kind}:${row}`, problem.message);
  }
  return result;
}

export function rowsFromRules(rules: readonly LinkRule[]): RuleRow[] {
  return rules.map((rule) => ({ pattern: rule.pattern, profiles: [...rule.profiles] }));
}

export function rowsFromWrappers(wrappers: readonly LinkWrapper[]): WrapperRow[] {
  return wrappers.map((w) => ({ pattern: w.pattern, opaque: w.parameter === '', parameter: w.parameter }));
}

/** A profile's name; rules may still name a deleted profile. */
export function profileName(id: string, profiles: readonly LinkProfile[]): string {
  return profiles.find((p) => p.id === id)?.name ?? 'Deleted profile';
}

/** Profiles a rule does not list yet, for its "Add profile" menu. */
export function profilesToAdd(row: RuleRow, profiles: readonly LinkProfile[]): LinkProfile[] {
  return profiles.filter((p) => !row.profiles.includes(p.id));
}

/** Moves a rule's profile one place earlier (the picker's order). */
export function moveProfileUp(row: RuleRow, index: number): void {
  if (index <= 0 || index >= row.profiles.length) return;
  const [moved] = row.profiles.splice(index, 1);
  if (moved !== undefined) row.profiles.splice(index - 1, 0, moved);
}
