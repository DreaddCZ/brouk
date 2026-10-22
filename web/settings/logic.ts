/** Settings page rules kept free of the DOM. */
import type { SettingItem } from './api.js';

export interface Section {
  readonly title: string;
  readonly items: readonly SettingItem[];
}

/** Items grouped by their section, in the order the sections first appear. */
export function sections(items: readonly SettingItem[]): Section[] {
  const result: { title: string; items: SettingItem[] }[] = [];
  for (const item of items) {
    let section = result.find((s) => s.title === item.section);
    if (section === undefined) {
      section = { title: item.section, items: [] };
      result.push(section);
    }
    section.items.push(item);
  }
  return result;
}

/**
 * Whether an item is shown: the custom search address only matters when
 * "Custom" is the search engine.
 */
export function isShown(item: SettingItem, all: readonly SettingItem[]): boolean {
  if (item.key === 'search.custom_template') {
    const engine = all.find((i) => i.key === 'search.engine');
    return engine?.value === 'custom' || item.value !== '';
  }
  return true;
}
