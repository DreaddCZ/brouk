/**
 * The bookmark manager page (011): all bookmarks of all profiles, a folder
 * tree on the left and the chosen folder (or the search results) on the
 * right. Rows are selected by clicking (⌘ toggles, Shift selects a range),
 * opened by double-click or Return, moved by dragging onto a folder or
 * between rows, and deleted with Delete (with Undo, also ⌘Z).
 */
import { cefSubscriber, cefTransport } from '../shared/bridge.js';
import { button, byId, element, errorText } from '../shared/dom.js';
import { wireHelpButtons } from '../shared/help.js';
import { BookmarksApi, type Profile } from './api.js';
import {
  clickSelection,
  displayName,
  dropTarget,
  folders,
  indexTree,
  isInside,
  isRoot,
  kBarId,
  pathLabel,
  search,
  selectionLabel,
  visibleChildren,
  type BookmarkNode,
  type BookmarkTree,
  type Located,
  type ProfileFilter,
} from './logic.js';

const api = new BookmarksApi(cefTransport(), cefSubscriber());
const kDragType = 'application/x-brouk-bookmarks';
const kToastMs = 6000;

let profiles: readonly Profile[] = [];
let currentProfile = '';
let tree: BookmarkTree | null = null;
let index = new Map<number, Located>();
let folderId = kBarId;
let query = '';
let filter: ProfileFilter = { kind: 'all' };
let selected = new Set<number>();
let anchor: number | null = null;
/** Row ids in the order shown (for Shift ranges and ⌘A). */
let order: number[] = [];
let toastTimer: ReturnType<typeof setTimeout> | undefined;
/** Site icons (013) by bookmark id. */
let icons: ReadonlyMap<string, string> = new Map();

function showError(message: string | null): void {
  const box = byId<HTMLParagraphElement>('error');
  box.textContent = message ?? '';
  box.hidden = message === null;
}

async function run(action: () => Promise<void>): Promise<void> {
  try {
    showError(null);
    await action();
  } catch (error) {
    showError(errorText(error));
  }
}

function profileById(id: string): Profile | undefined {
  return profiles.find((p) => p.id === id);
}

function profileBadge(node: BookmarkNode): HTMLElement | null {
  if (node.effective === '') return null;
  const profile = profileById(node.effective);
  const inherited = node.profile === '';
  const badge = element('span', { className: inherited ? 'profile inherited' : 'profile' });
  const dot = element('span', { className: 'dot' });
  dot.style.setProperty('--c', `var(--${profile?.color ?? 'grey'})`);
  badge.append(dot, element('span', { text: profile?.name ?? 'Deleted profile' }));
  badge.title = inherited ? 'Linked through its folder' : 'Linked to this profile';
  return badge;
}

// ---- Loading ----

async function load(): Promise<void> {
  const state = await api.state();
  profiles = state.profiles;
  currentProfile = state.currentProfile;
  tree = state.tree;
  index = indexTree(tree);
  if (!index.has(folderId) || index.get(folderId)?.node.type !== 'folder') folderId = kBarId;
  selected = new Set([...selected].filter((id) => index.has(id)));
  renderFilter();
  render();
  void loadIcons();
}

/** Icons come after the list, so it never waits for the history. */
async function loadIcons(): Promise<void> {
  try {
    icons = await api.icons();
  } catch {
    return; // The generic icons stay.
  }
  renderList();
}

let reloadPending = false;
/** Bookmarks changed somewhere (this page, the star, the bar…). */
function reloadSoon(): void {
  if (reloadPending) return;
  reloadPending = true;
  setTimeout(() => {
    reloadPending = false;
    void run(load);
  }, 50);
}

// ---- Rendering ----

function renderFilter(): void {
  const select = byId<HTMLSelectElement>('profile-filter');
  const value = select.value;
  select.replaceChildren();
  const add = (label: string, key: string): void => {
    const option = element('option', { text: label });
    option.value = key;
    select.append(option);
  };
  add('All profiles', 'all');
  add('General', 'general');
  for (const profile of profiles) add(profile.name, `p:${profile.id}`);
  select.value = [...select.options].some((o) => o.value === value) ? value : 'all';
}

function render(): void {
  renderTree();
  renderList();
  renderSelection();
}

function renderTree(): void {
  const nav = byId<HTMLElement>('tree');
  nav.replaceChildren();
  if (tree === null) return;
  for (const { node, depth } of folders(tree)) {
    const item = element('div', { className: 'tree-item' });
    item.role = 'treeitem';
    item.tabIndex = node.id === folderId && query === '' ? 0 : -1;
    item.setAttribute('aria-selected', String(node.id === folderId && query === ''));
    item.style.paddingLeft = `${10 + depth * 14}px`;
    item.append(element('span', { text: '📁' }), element('span', { className: 'name', text: displayName(node) }));
    const badge = profileBadge(node);
    if (badge !== null && node.profile !== '') {
      badge.replaceChildren(badge.firstChild ?? '');
      item.append(badge);
    }
    item.addEventListener('click', () => openFolder(node.id));
    item.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') openFolder(node.id);
    });
    addFolderDrop(item, node);
    nav.append(item);
  }
}

function openFolder(id: number): void {
  folderId = id;
  query = '';
  byId<HTMLInputElement>('query').value = '';
  selected = new Set();
  anchor = null;
  render();
}

function currentRows(): BookmarkNode[] {
  if (tree === null) return [];
  if (query !== '') return search(tree, query, filter);
  const folder = index.get(folderId)?.node;
  return folder === undefined ? [] : visibleChildren(folder, filter);
}

function renderList(): void {
  const list = byId<HTMLDivElement>('list');
  const empty = byId<HTMLParagraphElement>('empty');
  const title = byId<HTMLHeadingElement>('content-title');
  list.replaceChildren();
  const rows = currentRows();
  order = rows.map((row) => row.id);
  if (query !== '') {
    title.textContent = rows.length === 1 ? '1 result' : `${rows.length} results`;
  } else {
    title.textContent = index.get(folderId)?.node.name ?? '';
  }
  for (const node of rows) list.append(renderRow(node));
  empty.hidden = rows.length > 0;
  empty.textContent = query !== '' ? 'Nothing matches the search.' : 'This folder is empty.';
}

function renderRow(node: BookmarkNode): HTMLElement {
  const row = element('div', { className: 'row' });
  row.role = 'option';
  row.tabIndex = 0;
  row.dataset['id'] = String(node.id);
  row.setAttribute('aria-selected', String(selected.has(node.id)));
  row.draggable = true;
  const icon = element('span', { className: 'icon', text: node.type === 'folder' ? '📁' : '🌐' });
  const site = node.type === 'url' ? icons.get(String(node.id)) : undefined;
  if (site !== undefined) {
    const image = element('img');
    image.src = site;
    image.alt = '';
    icon.replaceChildren(image);
  }
  const main = element('div', { className: 'main' });
  main.append(element('span', { className: 'name', text: displayName(node) }));
  if (query !== '') {
    main.append(element('span', { className: 'path', text: pathLabel(index, node.id) }));
  } else if (node.type === 'url') {
    main.append(element('span', { className: 'url', text: node.url }));
  }
  if (node.type === 'url') main.title = node.url;
  const badge = profileBadge(node) ?? element('span');
  const edit = button('✎', () => openEditor(node), 'row-action');
  edit.title = 'Edit';
  edit.setAttribute('aria-label', `Edit ${displayName(node)}`);
  const remove = button('✕', () => void deleteItems([node.id]), 'row-action remove');
  remove.title = 'Delete';
  remove.setAttribute('aria-label', `Delete ${displayName(node)}`);
  for (const action of [edit, remove]) {
    action.addEventListener('dblclick', (event) => event.stopPropagation());
  }
  row.append(icon, main, badge, edit, remove);

  row.addEventListener('click', (event) => {
    if ((event.target as HTMLElement).closest('.row-action') !== null) return;
    selected = clickSelection(selected, order, anchor, node.id, { toggle: event.metaKey, range: event.shiftKey });
    if (!event.shiftKey) anchor = node.id;
    renderSelectionOnly();
  });
  row.addEventListener('dblclick', (event) => activate(node, event.metaKey));
  row.addEventListener('keydown', (event) => {
    if (event.key === 'Enter') activate(node, event.metaKey);
  });
  addRowDrag(row, node);
  return row;
}

function activate(node: BookmarkNode, newTab: boolean): void {
  if (node.type === 'folder') {
    openFolder(node.id);
    return;
  }
  void run(() => api.open(node.id, newTab));
}

function renderSelectionOnly(): void {
  for (const row of byId<HTMLDivElement>('list').querySelectorAll<HTMLElement>('.row')) {
    row.setAttribute('aria-selected', String(selected.has(Number(row.dataset['id']))));
  }
  renderSelection();
}

function renderSelection(): void {
  const label = byId<HTMLSpanElement>('selection');
  const count = selected.size;
  label.hidden = count === 0;
  label.textContent = selectionLabel(count);
  byId<HTMLButtonElement>('delete-selected').hidden = count === 0;
  byId<HTMLButtonElement>('edit-selected').hidden = count !== 1;
}

// ---- Drag and drop ----

function draggedIds(event: DragEvent): number[] | null {
  const data = event.dataTransfer?.getData(kDragType);
  if (data === undefined || data === '') return null;
  try {
    const parsed: unknown = JSON.parse(data);
    if (!Array.isArray(parsed)) return null;
    const ids: unknown[] = parsed;
    return ids.every((id) => typeof id === 'number') ? (ids as number[]) : null;
  } catch {
    return null;
  }
}

function hasBookmarkDrag(event: DragEvent): boolean {
  return event.dataTransfer?.types.includes(kDragType) ?? false;
}

function addRowDrag(row: HTMLElement, node: BookmarkNode): void {
  row.addEventListener('dragstart', (event) => {
    if (!selected.has(node.id)) {
      selected = new Set([node.id]);
      anchor = node.id;
      renderSelectionOnly();
    }
    const ids = order.filter((id) => selected.has(id));
    event.dataTransfer?.setData(kDragType, JSON.stringify(ids));
    if (node.type === 'url') {
      event.dataTransfer?.setData('text/uri-list', node.url);
      event.dataTransfer?.setData('text/plain', node.url);
    }
    if (event.dataTransfer) event.dataTransfer.effectAllowed = 'copyMove';
  });
  const position = (event: DragEvent): 'before' | 'after' | 'into' => {
    const box = row.getBoundingClientRect();
    const y = (event.clientY - box.top) / box.height;
    if (node.type === 'folder' && y > 0.25 && y < 0.75) return 'into';
    if (query !== '') return node.type === 'folder' ? 'into' : 'after';
    return y < 0.5 ? 'before' : 'after';
  };
  const clear = (): void => row.classList.remove('drop-before', 'drop-after', 'drop-into');
  row.addEventListener('dragover', (event) => {
    if (!hasBookmarkDrag(event)) return;
    const where = position(event);
    // In search results only folders take drops.
    if (query !== '' && where !== 'into') return;
    event.preventDefault();
    clear();
    row.classList.add(`drop-${where}`);
  });
  row.addEventListener('dragleave', clear);
  row.addEventListener('drop', (event) => {
    clear();
    const ids = draggedIds(event);
    if (ids === null) return;
    event.preventDefault();
    const where = position(event);
    const folder = query === '' ? index.get(folderId)?.node : undefined;
    const target =
      where === 'into' || folder === undefined ? { parent: node.id, index: null } : dropTarget(folder, node, where);
    if (ids.some((id) => isInside(index, target.parent, id))) {
      showError('A folder cannot be moved into itself.');
      return;
    }
    void run(() => api.move(ids, target.parent, target.index));
  });
}

function addFolderDrop(item: HTMLElement, node: BookmarkNode): void {
  item.addEventListener('dragover', (event) => {
    if (!hasBookmarkDrag(event)) return;
    event.preventDefault();
    item.classList.add('drop-into');
  });
  item.addEventListener('dragleave', () => item.classList.remove('drop-into'));
  item.addEventListener('drop', (event) => {
    item.classList.remove('drop-into');
    const ids = draggedIds(event);
    if (ids === null) return;
    event.preventDefault();
    if (ids.some((id) => isInside(index, node.id, id))) {
      showError('A folder cannot be moved into itself.');
      return;
    }
    void run(() => api.move(ids, node.id, null));
  });
}

// ---- Deleting and undo ----

async function deleteItems(ids: readonly number[]): Promise<void> {
  const removable = ids.filter((id) => !isRoot(id));
  if (removable.length === 0) return;
  await run(async () => {
    await api.remove(removable);
    selected = new Set();
    showToast(removable.length === 1 ? 'Deleted 1 item' : `Deleted ${removable.length} items`);
  });
}

function showToast(message: string): void {
  const toast = byId<HTMLDivElement>('toast');
  toast.replaceChildren(element('span', { text: message }), button('Undo', () => void undo()));
  toast.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => {
    toast.hidden = true;
  }, kToastMs);
}

async function undo(): Promise<void> {
  byId<HTMLDivElement>('toast').hidden = true;
  await run(async () => {
    await api.undo();
  });
}

// ---- Editor ----

type EditorTarget =
  | { readonly kind: 'edit'; readonly node: BookmarkNode }
  | { readonly kind: 'add'; readonly folder: boolean; readonly parent: number };

let editorTarget: EditorTarget | null = null;
/** The profile chosen while the folder does not decide it. */
let editorOwnProfile = '';

function fillProfiles(select: HTMLSelectElement): void {
  select.replaceChildren();
  const general = element('option', { text: 'General (all profiles)' });
  general.value = '';
  select.append(general);
  for (const profile of profiles) {
    const option = element('option', { text: profile.name });
    option.value = profile.id;
    select.append(option);
  }
}

function updateEditorProfile(): void {
  const folderSelect = byId<HTMLSelectElement>('editor-folder');
  const profileSelect = byId<HTMLSelectElement>('editor-profile');
  const hint = byId<HTMLParagraphElement>('editor-hint');
  const parent = index.get(Number(folderSelect.value))?.node;
  const locked = parent?.effective ?? '';
  profileSelect.disabled = locked !== '';
  profileSelect.value = locked !== '' ? locked : editorOwnProfile;
  hint.textContent =
    locked !== ''
      ? 'The folder is linked to this profile; everything in it follows the folder.'
      : 'General bookmarks show in every profile; linked ones only in their profile.';
}

function openEditor(node: BookmarkNode): void {
  const parent = index.get(node.id)?.parent;
  if (parent === null || parent === undefined) return;
  showEditor({ kind: 'edit', node }, node.type === 'folder' ? 'Edit folder' : 'Edit bookmark', {
    name: node.name,
    url: node.url,
    parent: parent.id,
    profile: node.profile,
    folder: node.type === 'folder',
  });
}

function showEditor(
  target: EditorTarget,
  title: string,
  values: { name: string; url: string; parent: number; profile: string; folder: boolean },
): void {
  if (tree === null) return;
  editorTarget = target;
  editorOwnProfile = values.profile;
  byId<HTMLHeadingElement>('editor-title').textContent = title;
  byId<HTMLInputElement>('editor-name').value = values.name;
  byId<HTMLInputElement>('editor-url').value = values.url;
  byId<HTMLLabelElement>('editor-url-row').hidden = values.folder;
  const folderSelect = byId<HTMLSelectElement>('editor-folder');
  folderSelect.replaceChildren();
  const exclude = target.kind === 'edit' && target.node.type === 'folder' ? target.node.id : undefined;
  for (const { node, depth } of folders(tree, exclude)) {
    const option = element('option', { text: `${' '.repeat(depth)}${displayName(node)}` });
    option.value = String(node.id);
    folderSelect.append(option);
  }
  folderSelect.value = String(values.parent);
  fillProfiles(byId<HTMLSelectElement>('editor-profile'));
  updateEditorProfile();
  byId<HTMLParagraphElement>('editor-error').hidden = true;
  byId<HTMLDivElement>('editor').hidden = false;
  byId<HTMLInputElement>('editor-name').focus();
  byId<HTMLInputElement>('editor-name').select();
}

function closeEditor(): void {
  byId<HTMLDivElement>('editor').hidden = true;
  editorTarget = null;
}

async function saveEditor(): Promise<void> {
  const target = editorTarget;
  if (target === null) return;
  const name = byId<HTMLInputElement>('editor-name').value.trim();
  const url = byId<HTMLInputElement>('editor-url').value.trim();
  const parent = Number(byId<HTMLSelectElement>('editor-folder').value);
  const profileSelect = byId<HTMLSelectElement>('editor-profile');
  // Inside a linked folder the item follows the folder (no link of its own).
  const profile = profileSelect.disabled ? '' : profileSelect.value;
  const folder = target.kind === 'edit' ? target.node.type === 'folder' : target.folder;
  if (!folder && url === '') {
    showEditorError('Enter the address.');
    return;
  }
  try {
    if (target.kind === 'edit') {
      await api.edit({ id: target.node.id, name, url, profile, parent });
    } else {
      await api.add({ parent, index: null, folder, name: name === '' && folder ? 'New folder' : name, url, profile });
    }
    closeEditor();
  } catch (error) {
    showEditorError(errorText(error));
  }
}

function showEditorError(message: string): void {
  const box = byId<HTMLParagraphElement>('editor-error');
  box.textContent = message;
  box.hidden = false;
}

function addTarget(): number {
  return query === '' ? folderId : kBarId;
}

// ---- Setup ----

function setUp(): void {
  byId<HTMLInputElement>('query').addEventListener('input', (event) => {
    query = (event.target as HTMLInputElement).value.trim();
    selected = new Set();
    anchor = null;
    render();
  });
  byId<HTMLSelectElement>('profile-filter').addEventListener('change', (event) => {
    const value = (event.target as HTMLSelectElement).value;
    filter = value === 'all' ? { kind: 'all' } : value === 'general' ? { kind: 'general' } : { kind: 'profile', id: value.slice(2) };
    selected = new Set();
    render();
  });
  byId<HTMLButtonElement>('add-bookmark').addEventListener('click', () =>
    showEditor({ kind: 'add', folder: false, parent: addTarget() }, 'Add bookmark', {
      name: '',
      url: '',
      parent: addTarget(),
      profile: currentProfile,
      folder: false,
    }),
  );
  byId<HTMLButtonElement>('add-folder').addEventListener('click', () =>
    showEditor({ kind: 'add', folder: true, parent: addTarget() }, 'Add folder', {
      name: 'New folder',
      url: '',
      parent: addTarget(),
      profile: currentProfile,
      folder: true,
    }),
  );
  byId<HTMLButtonElement>('delete-selected').addEventListener('click', () => void deleteItems([...selected]));
  byId<HTMLButtonElement>('edit-selected').addEventListener('click', () => {
    const [id] = [...selected];
    const node = id === undefined ? undefined : index.get(id)?.node;
    if (node !== undefined) openEditor(node);
  });
  byId<HTMLButtonElement>('export').addEventListener('click', () => void run(async () => void (await api.exportHtml())));
  byId<HTMLButtonElement>('import').addEventListener('click', () =>
    void run(async () => {
      const result = await api.importHtml();
      if (result.imported) {
        showToastInfo(`Imported ${result.bookmarks} bookmarks and ${result.folders} folders into “Imported” under Other bookmarks.`);
      }
    }),
  );
  byId<HTMLSelectElement>('editor-folder').addEventListener('change', updateEditorProfile);
  byId<HTMLSelectElement>('editor-profile').addEventListener('change', (event) => {
    editorOwnProfile = (event.target as HTMLSelectElement).value;
  });
  byId<HTMLButtonElement>('editor-cancel').addEventListener('click', closeEditor);
  byId<HTMLFormElement>('editor-form').addEventListener('submit', (event) => {
    event.preventDefault();
    void saveEditor();
  });
  document.addEventListener('keydown', (event) => {
    if (!byId<HTMLDivElement>('editor').hidden) {
      if (event.key === 'Escape') closeEditor();
      return;
    }
    const typing = event.target instanceof HTMLInputElement || event.target instanceof HTMLSelectElement;
    if (typing) return;
    if (event.metaKey && event.key === 'z') {
      event.preventDefault();
      void undo();
    } else if (event.metaKey && event.key === 'a') {
      event.preventDefault();
      selected = new Set(order);
      renderSelectionOnly();
    } else if ((event.key === 'Delete' || event.key === 'Backspace') && selected.size > 0) {
      event.preventDefault();
      void deleteItems([...selected]);
    }
  });
  api.watch(reloadSoon);
  void run(load);
}

function showToastInfo(message: string): void {
  const toast = byId<HTMLDivElement>('toast');
  toast.replaceChildren(element('span', { text: message }), button('OK', () => (toast.hidden = true)));
  toast.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => {
    toast.hidden = true;
  }, kToastMs);
}

setUp();

wireHelpButtons();
