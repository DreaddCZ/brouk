/**
 * The open-source licences page (023), brouk://credits in a tab. Shows the
 * data scripts/credits.mjs wrote at build time (credits/data.js); asks the
 * browser for nothing.
 */
import { byId, element, errorText } from '../shared/dom.js';
import { wireHelpButtons } from '../shared/help.js';
import { filterComponents, parseCredits, safeHomepage, type Component } from './logic.js';

const broukSection = byId<HTMLElement>('brouk');
const filter = byId<HTMLInputElement>('filter');
const count = byId<HTMLParagraphElement>('count');
const errorBox = byId<HTMLParagraphElement>('error');
const list = byId<HTMLDivElement>('components');

// Generated after the TypeScript build, so not type-checked: parseCredits
// checks it.
const kDataModule = './data.js';

/** `href` is an http(s) address (safeHomepage) or a plain page name. */
function link(text: string, href: string): HTMLAnchorElement {
  const node = element('a', { text });
  node.href = href; // nosemgrep: ts-url-sink -- only checked addresses, see above
  return node;
}

/** The homepage and further notices of a component, as links. */
function links(component: Component): HTMLElement {
  const box = element('div', { className: 'links' });
  const homepage = safeHomepage(component.homepage);
  if (homepage !== null) box.append(link('Project page', homepage));
  if (component.notices !== '') {
    const label = component.notices === 'chromium' ? 'Chromium credits' : 'Full notices';
    box.append(link(label, component.notices));
  }
  return box;
}

/** The licence texts, made when first unfolded: most are never read. */
function texts(component: Component, into: HTMLElement): void {
  for (const text of component.texts) {
    into.append(element('h3', { className: 'file', text: text.file }));
    into.append(element('pre', { className: 'licence-text', text: text.text }));
  }
}

function componentDetails(component: Component): HTMLDetailsElement {
  const details = element('details');
  const summary = element('summary');
  summary.append(
    element('span', { className: 'name', text: component.name }),
    element('span', { className: 'version', text: component.version }),
    element('span', { className: 'licence', text: component.licence }),
  );
  details.append(summary, links(component));
  details.addEventListener('toggle', () => {
    if (details.open && details.querySelector('pre') === null) texts(component, details);
  });
  return details;
}

function showBrouk(brouk: Component): void {
  const title = element('h2', { text: 'Brouk' });
  title.id = 'brouk-title';
  const about = element('p', {
    text: 'Free for anyone to use, change and share, for personal or work use, under the Apache License 2.0.',
  });
  const details = componentDetails({ ...brouk, name: 'Licence', version: '', licence: brouk.licence });
  broukSection.append(title, about, details);
}

function showComponents(components: Component[]): void {
  const groups = filterComponents(components, filter.value);
  let shown = 0;
  list.replaceChildren();
  for (const [group, members] of groups) {
    list.append(element('h2', { className: 'group', text: group }));
    for (const component of members) list.append(componentDetails(component));
    shown += members.length;
  }
  count.textContent = filter.value.trim() === ''
    ? `${components.length} components`
    : `${shown} of ${components.length} components`;
}

async function start(): Promise<void> {
  wireHelpButtons();
  try {
    const module: unknown = await import(kDataModule);
    const credits = parseCredits(
      typeof module === 'object' && module !== null ? (module as Record<string, unknown>)['credits'] : null);
    if (credits === null) throw new Error('The licence data of this build is damaged.');
    showBrouk(credits.brouk);
    showComponents(credits.components);
    filter.addEventListener('input', () => showComponents(credits.components));
  } catch (error) {
    errorBox.textContent = `Could not show the licences: ${errorText(error)}`;
    errorBox.hidden = false;
  }
}

void start();
