/**
 * Site permissions in settings (012): the sites the chosen profile has
 * allowed or blocked something for, each permission changeable (Ask
 * removes the decision) and the whole site resettable, as in the address
 * bar's site information.
 */
import { cefTransport } from '../shared/bridge.js';
import { button, byId, element, errorText } from '../shared/dom.js';
import { PermissionsApi, type PermissionSetting, type PermissionSite, type PermissionsState } from './permissions_api.js';
import { filterSites, siteLabel } from './permissions_logic.js';

const api = new PermissionsApi(cefTransport());
/** Whose permissions are shown; null while the topic is not. */
let shownProfile: string | null = null;
let state: PermissionsState | null = null;

function showError(message: string | null): void {
  const box = byId<HTMLParagraphElement>('site-permissions-error');
  box.textContent = message ?? '';
  box.hidden = message === null;
}

async function act(action: () => Promise<void>): Promise<void> {
  try {
    await action();
    showError(null);
  } catch (error) {
    showError(errorText(error));
  }
  await load();
}

function settingSelect(profile: string, site: PermissionSite, id: string, name: string, value: string): HTMLSelectElement {
  const select = element('select');
  select.setAttribute('aria-label', `${name} for ${siteLabel(site.origin)}`);
  for (const [setting, label] of [
    ['allow', 'Allow'],
    ['block', 'Block'],
    ['ask', 'Ask (default)'],
  ] as const) {
    const option = element('option', { text: label });
    option.value = setting;
    option.selected = setting === value;
    select.append(option);
  }
  select.addEventListener('change', () => {
    const setting = select.value as PermissionSetting;
    void act(() => api.set(profile, site.origin, id, setting));
  });
  return select;
}

function siteBox(profile: string, site: PermissionSite): HTMLElement {
  const box = element('div', { className: 'permission-site' });
  const head = element('div', { className: 'permission-site-head' });
  const reset = button('Reset', () => void act(() => api.reset(profile, site.origin)));
  reset.setAttribute('aria-label', `Reset permissions of ${siteLabel(site.origin)}`);
  head.append(element('strong', { text: siteLabel(site.origin) }), reset);
  box.append(head);
  for (const permission of site.permissions) {
    const name = state?.kinds.find((k) => k.id === permission.id)?.name ?? permission.id;
    const row = element('div', { className: 'permission-row' });
    row.append(element('span', { text: name }), settingSelect(profile, site, permission.id, name, permission.setting));
    box.append(row);
  }
  return box;
}

function render(): void {
  const profile = shownProfile;
  const panel = byId('site-permissions');
  panel.hidden = profile === null;
  if (profile === null) return;
  const query = byId<HTMLInputElement>('site-permissions-search').value;
  const sites = filterSites(state?.sites ?? [], query);
  byId('site-permissions-list').replaceChildren(...sites.map((site) => siteBox(profile, site)));
  const empty = byId('site-permissions-empty');
  empty.hidden = state === null || sites.length > 0;
  empty.textContent =
    (state?.sites.length ?? 0) === 0
      ? 'No site has been allowed or blocked anything in this profile.'
      : 'No site matches the search.';
}

async function load(): Promise<void> {
  const profile = shownProfile;
  if (profile === null) return;
  try {
    const loaded = await api.state(profile);
    if (profile !== shownProfile) return;  // Another profile was chosen meanwhile.
    state = loaded;
    showError(null);
  } catch (error) {
    showError(errorText(error));
  }
  render();
}

/** Shows `profile`'s sites (reloaded: decisions change in the address bar too), or hides the list. */
export function showSitePermissions(profile: string | null): void {
  if (profile !== shownProfile) state = null;
  shownProfile = profile;
  render();
  void load();
}

export function startPermissionsPanel(): void {
  byId('site-permissions-search').addEventListener('input', render);
}
