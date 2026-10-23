/**
 * Our own developer tools (006), one internal page in the dock; the dock's
 * tabs choose the tool with the URL fragment (#site-data, ...).
 */
import { cefTransport } from '../shared/bridge.js';
import { byId } from '../shared/dom.js';
import { ToolsApi } from './api.js';
import { SiteDataTool } from './site_data.js';

const api = new ToolsApi(cefTransport());
const root = byId<HTMLElement>('tool');
const siteData = new SiteDataTool(api, root);

function show(): void {
  const tool = location.hash.slice(1);
  if (tool === 'site-data' || tool === '') {
    document.title = 'Site data';
    void siteData.refresh();
  }
}

window.addEventListener('hashchange', show);
// The tab may have navigated meanwhile: refresh when shown again.
window.addEventListener('focus', () => {
  if (!siteData.busy) show();
});
show();
