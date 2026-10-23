/**
 * The bookmark manager's logic (011), kept free of the DOM so it is tested
 * with Node: the tree as the browser sends it, filters, search and
 * selection.
 */

export interface BookmarkNode {
  readonly id: number;
  readonly type: 'folder' | 'url';
  readonly name: string;
  /** Bookmarks only; '' for folders. */
  readonly url: string;
  /** The item's own link ('' for general). */
  readonly profile: string;
  /** The profile it belongs to, inherited from a linked folder or its own. */
  readonly effective: string;
  readonly added: number;
  /** Folders only. */
  readonly children: readonly BookmarkNode[];
}

export interface BookmarkTree {
  readonly bar: BookmarkNode;
  readonly other: BookmarkNode;
}

/** Which bookmarks the page shows: all, general ones, or one profile's. */
export type ProfileFilter = { readonly kind: 'all' } | { readonly kind: 'general' } | { readonly kind: 'profile'; readonly id: string };

export interface Located {
  readonly node: BookmarkNode;
  /** The folder it is in; null for the two top-level folders. */
  readonly parent: BookmarkNode | null;
}

/** Every item by id, with its folder. */
export function indexTree(tree: BookmarkTree): Map<number, Located> {
  const index = new Map<number, Located>();
  const visit = (node: BookmarkNode, parent: BookmarkNode | null): void => {
    index.set(node.id, { node, parent });
    for (const child of node.children) visit(child, node);
  };
  visit(tree.bar, null);
  visit(tree.other, null);
  return index;
}

/** The folders from the top down to `id` (inclusive). */
export function pathTo(index: ReadonlyMap<number, Located>, id: number): BookmarkNode[] {
  const path: BookmarkNode[] = [];
  for (let at = index.get(id); at !== undefined; at = at.parent === null ? undefined : index.get(at.parent.id)) {
    path.unshift(at.node);
  }
  return path;
}

/** `id` is `ancestor` or inside it. */
export function isInside(index: ReadonlyMap<number, Located>, id: number, ancestor: number): boolean {
  return pathTo(index, id).some((node) => node.id === ancestor);
}

export function matchesFilter(node: BookmarkNode, filter: ProfileFilter): boolean {
  switch (filter.kind) {
    case 'all':
      return true;
    case 'general':
      return node.effective === '';
    case 'profile':
      return node.effective === filter.id;
  }
}

/**
 * A folder's items to list. Folders stay when anything inside them matches,
 * so a general folder holding one profile's bookmarks is still reachable.
 */
export function visibleChildren(folder: BookmarkNode, filter: ProfileFilter): BookmarkNode[] {
  return folder.children.filter((child) => matchesFilter(child, filter) || (child.type === 'folder' && containsMatch(child, filter)));
}

function containsMatch(folder: BookmarkNode, filter: ProfileFilter): boolean {
  return folder.children.some((child) => matchesFilter(child, filter) || (child.type === 'folder' && containsMatch(child, filter)));
}

/** Words of a search, lower case. */
function words(query: string): string[] {
  return query.toLowerCase().split(/\s+/).filter((word) => word !== '');
}

/**
 * Bookmarks and folders whose name (or a bookmark's address) contains every
 * word of `query`, in tree order.
 */
export function search(tree: BookmarkTree, query: string, filter: ProfileFilter): BookmarkNode[] {
  const wanted = words(query);
  if (wanted.length === 0) return [];
  const found: BookmarkNode[] = [];
  const visit = (node: BookmarkNode): void => {
    for (const child of node.children) {
      const text = `${child.name}\n${child.url}`.toLowerCase();
      if (matchesFilter(child, filter) && wanted.every((word) => text.includes(word))) found.push(child);
      if (child.type === 'folder') visit(child);
    }
  };
  visit(tree.bar);
  visit(tree.other);
  return found;
}

export interface FolderEntry {
  readonly node: BookmarkNode;
  readonly depth: number;
}

/** Every folder in tree order, for the tree and the folder pickers. */
export function folders(tree: BookmarkTree, exclude?: number): FolderEntry[] {
  const result: FolderEntry[] = [];
  const visit = (node: BookmarkNode, depth: number): void => {
    if (node.id === exclude) return;
    result.push({ node, depth });
    for (const child of node.children) {
      if (child.type === 'folder') visit(child, depth + 1);
    }
  };
  visit(tree.bar, 0);
  visit(tree.other, 0);
  return result;
}

/** What a row shows as its name. */
export function displayName(node: BookmarkNode): string {
  return node.name !== '' ? node.name : node.url;
}

/**
 * The selection after a click on `id`: alone, toggled with ⌘, or the range
 * from the anchor with Shift (in the order shown).
 */
export function clickSelection(
  selected: ReadonlySet<number>,
  order: readonly number[],
  anchor: number | null,
  id: number,
  modifiers: { readonly toggle: boolean; readonly range: boolean },
): Set<number> {
  if (modifiers.range && anchor !== null) {
    const from = order.indexOf(anchor);
    const to = order.indexOf(id);
    if (from >= 0 && to >= 0) {
      const [start, end] = from < to ? [from, to] : [to, from];
      const next = modifiers.toggle ? new Set(selected) : new Set<number>();
      for (const item of order.slice(start, end + 1)) next.add(item);
      return next;
    }
  }
  if (modifiers.toggle) {
    const next = new Set(selected);
    if (next.has(id)) next.delete(id);
    else next.add(id);
    return next;
  }
  return new Set([id]);
}

/**
 * Where a drop on a row of folder `folder` goes, as the browser counts it
 * (the folder's index before the move): before or after the row, or into
 * the row when it is a folder and the drop is on its middle.
 */
export function dropTarget(
  folder: BookmarkNode,
  row: BookmarkNode,
  position: 'before' | 'after' | 'into',
): { readonly parent: number; readonly index: number | null } {
  if (position === 'into' && row.type === 'folder') return { parent: row.id, index: null };
  const at = folder.children.findIndex((child) => child.id === row.id);
  if (at < 0) return { parent: folder.id, index: null };
  return { parent: folder.id, index: position === 'after' ? at + 1 : at };
}

/** "Bookmarks bar › Work › Jira" for search results. */
export function pathLabel(index: ReadonlyMap<number, Located>, id: number): string {
  return pathTo(index, id)
    .slice(0, -1)
    .map((node) => node.name)
    .join(' › ');
}

/** "3 items selected" etc. */
export function selectionLabel(count: number): string {
  return count === 1 ? '1 item selected' : `${count} items selected`;
}

/** Folders are listed with a fixed name for the two at the top. */
export const kBarId = 1;
export const kOtherId = 2;
export function isRoot(id: number): boolean {
  return id === kBarId || id === kOtherId;
}
