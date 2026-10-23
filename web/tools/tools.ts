/**
 * Our own developer tools (006), one internal page in the dock; the dock's
 * tabs choose the tool with the URL fragment (#site-data, ...).
 */
import { cefTransport } from '../shared/bridge.js';
import { byId } from '../shared/dom.js';
import { ToolsApi } from './api.js';
import { RequestsTool } from './requests.js';
import { RulesTool } from './rules.js';
import { SiteDataTool } from './site_data.js';
import { TokensTool } from './tokens.js';

const api = new ToolsApi(cefTransport());
const root = byId<HTMLElement>('tool');
const siteData = new SiteDataTool(api, root);
const requests = new RequestsTool(api, root);
const tokens = new TokensTool(api, root);
const rules = new RulesTool(api, root);
let current = '';

function show(): void {
  const tool = location.hash.slice(1) || 'site-data';
  if (tool !== current) {
    requests.stop();
    rules.stop();
    current = tool;
  } else if (tool === 'requests' || tool === 'rules') {
    return;  // Already up to date (they refresh themselves).
  }
  if (tool === 'requests') {
    document.title = 'Requests';
    requests.start();
  } else if (tool === 'rules') {
    document.title = 'Request rules';
    rules.start();
  } else if (tool === 'tokens') {
    document.title = 'Tokens';
    void tokens.refresh();
  } else {
    document.title = 'Site data';
    void siteData.refresh();
  }
}

window.addEventListener('hashchange', show);
// The tab may have navigated meanwhile: refresh when shown again.
window.addEventListener('focus', () => {
  if (!siteData.busy && !tokens.busy && !rules.busy) show();
});
show();
