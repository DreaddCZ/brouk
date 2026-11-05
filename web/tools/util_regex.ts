/** Regular expressions (027), with JavaScript's syntax. */

export interface RegexMatch {
  readonly index: number;
  readonly text: string;
  /** Numbered groups first ("1", "2"…), then named ones; undefined when a group did not take part. */
  readonly groups: readonly { readonly name: string; readonly value: string | undefined }[];
}

export type RegexResult =
  | { readonly ok: true; readonly matches: readonly RegexMatch[]; readonly replaced: string | null; readonly limited: boolean }
  | { readonly ok: false; readonly error: string };

/** At most this many matches are listed. */
export const kMaxMatches = 1000;

/** Matches of `pattern` in `text`; with `replacement`, also the replaced text. */
export function runRegex(pattern: string, flags: string, text: string, replacement: string | null): RegexResult {
  let regex: RegExp;
  try {
    regex = new RegExp(pattern, flags);
  } catch (error) {
    return { ok: false, error: error instanceof Error ? error.message : String(error) };
  }
  const matches: RegexMatch[] = [];
  const toMatch = (m: RegExpExecArray): RegexMatch => ({
    index: m.index,
    text: m[0],
    groups: [
      ...m.slice(1).map((value, i) => ({ name: String(i + 1), value })),
      ...Object.entries(m.groups ?? {}).map(([name, value]) => ({ name, value })),
    ],
  });
  let limited = false;
  if (regex.global || regex.sticky) {
    regex.lastIndex = 0;
    for (;;) {
      const m = regex.exec(text);
      if (m === null) break;
      if (matches.length === kMaxMatches) {
        limited = true;
        break;
      }
      matches.push(toMatch(m));
      // An empty match must not stop the search at the same place.
      if (m[0] === '') regex.lastIndex += 1;
    }
  } else {
    const m = regex.exec(text);
    if (m !== null) matches.push(toMatch(m));
  }
  regex.lastIndex = 0;
  return { ok: true, matches, replaced: replacement === null ? null : text.replace(regex, replacement), limited };
}

/** Which flags are valid here: g i m s u y d (v instead of u). */
export function invalidFlags(flags: string): string | null {
  for (const flag of flags) {
    if (!'dgimsuvy'.includes(flag)) return `Unknown flag “${flag}”.`;
  }
  if (new Set(flags).size !== flags.length) return 'A flag is repeated.';
  return null;
}
