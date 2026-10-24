/** The ad blocking panel's logic (007), free of the DOM for tests. */
import type { FilterCategory, FilterList } from './adblock_api.js';

/** Lists by category, in the categories' order; empty categories left out. */
export function groupLists(
  categories: readonly FilterCategory[],
  lists: readonly FilterList[],
): { category: FilterCategory; lists: FilterList[] }[] {
  return categories
    .map((category) => ({ category, lists: lists.filter((l) => l.category === category.id) }))
    .filter((group) => group.lists.length > 0);
}

/** "Updated 3 hours ago", "Not downloaded yet", or the error. */
export function listStatus(list: FilterList, now: number): string {
  if (list.error !== '') {
    return list.updated > 0 ? `${list.error} (keeps the version from ${ago(list.updated, now)})` : list.error;
  }
  if (list.updated === 0) return list.enabled ? 'Not downloaded yet' : '';
  return `Updated ${ago(list.updated, now)}`;
}

function ago(when: number, now: number): string {
  const minutes = Math.max(0, Math.floor((now - when) / 60));
  if (minutes < 1) return 'just now';
  if (minutes < 60) return minutes === 1 ? '1 minute ago' : `${minutes} minutes ago`;
  const hours = Math.round(minutes / 60);
  if (hours < 48) return hours === 1 ? '1 hour ago' : `${hours} hours ago`;
  return `${Math.round(hours / 24)} days ago`;
}

/** Whether text could be a list's address (the browser checks it again). */
export function isListUrl(text: string): boolean {
  try {
    const url = new URL(text.trim());
    return url.protocol === 'https:' || url.protocol === 'http:';
  } catch {
    return false;
  }
}
