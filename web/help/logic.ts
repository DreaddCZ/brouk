/** The help page's search (016): pure functions, tested with Node. */

/** Lowercase without accents ("Nastavení" -> "nastaveni"), as the palette folds. */
export function fold(text: string): string {
  return text.normalize('NFD').replace(/[̀-ͯ]/g, '').toLowerCase();
}

/** The words of a query, folded. */
export function queryWords(query: string): string[] {
  return fold(query)
    .split(/\s+/)
    .filter((word) => word !== '');
}

/** Whether `text` contains every word (an empty query matches everything). */
export function matchesAll(text: string, words: readonly string[]): boolean {
  const folded = fold(text);
  return words.every((word) => folded.includes(word));
}

/**
 * Where the words occur in `text`, as [start, end) ranges of `text` itself,
 * sorted and without overlaps. Folding keeps one character per character
 * for the letters it changes, so the indices map back one to one.
 */
export function matchRanges(text: string, words: readonly string[]): Array<[number, number]> {
  // Fold character by character so positions stay aligned with `text`.
  const chars = [...text];
  const folded = chars.map((c) => fold(c) || c);
  const offsets: number[] = [];
  let joined = '';
  let position = 0;
  for (let i = 0; i < chars.length; i++) {
    offsets.push(position);
    joined += folded[i];
    position += (chars[i] ?? '').length;
  }
  offsets.push(position);
  // Map an index in `joined` back to `text`.
  const back: number[] = [];
  for (let i = 0; i < chars.length; i++) {
    for (let k = 0; k < (folded[i] ?? '').length; k++) back.push(offsets[i] ?? 0);
  }
  back.push(position);
  const ranges: Array<[number, number]> = [];
  for (const word of words) {
    for (let at = joined.indexOf(word); at !== -1; at = joined.indexOf(word, at + word.length)) {
      ranges.push([back[at] ?? 0, back[at + word.length] ?? position]);
    }
  }
  ranges.sort((a, b) => a[0] - b[0]);
  const merged: Array<[number, number]> = [];
  for (const range of ranges) {
    const last = merged[merged.length - 1];
    if (last !== undefined && range[0] <= last[1]) last[1] = Math.max(last[1], range[1]);
    else merged.push([range[0], range[1]]);
  }
  return merged;
}
