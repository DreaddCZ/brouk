/**
 * Our own developer tools (006), one internal page in the dock; the dock's
 * tabs choose the tool with the URL fragment (#site-data, ...).
 */
import { cefTransport } from '../shared/bridge.js';
import { byId } from '../shared/dom.js';
import { openHelp } from '../shared/help.js';
import { ToolsApi } from './api.js';
import { RequestsTool } from './requests.js';
import { RulesTool } from './rules.js';
import { SiteDataTool } from './site_data.js';
import { TokensTool } from './tokens.js';
import { UtilitiesTool } from './utilities.js';

const api = new ToolsApi(cefTransport());
const root = byId<HTMLElement>('tool');
const siteData = new SiteDataTool(api, root);
const requests = new RequestsTool(api, root);
const tokens = new TokensTool(api, root);
const rules = new RulesTool(api, root);
const utilities = new UtilitiesTool(api, root);
let current = '';

/** "#utilities/base64" -> ["utilities", "base64"]. */
function route(): [string, string | null] {
  const [tool, sub] = location.hash.slice(1).split('/', 2);
  return [tool || 'site-data', sub ?? null];
}

/** Text sent with "Decode with…" (027), if any, into its utility. */
async function takeInput(): Promise<void> {
  const input = await api.takeUtilityInput().catch(() => null);
  if (input !== null) utilities.show(input.utility, input.text);
}

function show(): void {
  const [tool, utility] = route();
  if (tool === 'utilities') {
    if (tool !== current) {
      requests.stop();
      rules.stop();
      current = tool;
    }
    utilities.show(utility, null);
    void takeInput();
    return;
  }
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

// Help for the tool shown (016).
byId<HTMLButtonElement>('tool-help').addEventListener('click', () => {
  const [tool] = route();
  if (tool === 'utilities') openHelp('devtools-utilities');
  else if (tool === 'requests') openHelp('devtools-requests');
  else if (tool === 'rules') openHelp('devtools-request-rules');
  else if (tool === 'tokens') openHelp('devtools-tokens');
  else openHelp('devtools-site-data');
});

window.addEventListener('hashchange', show);
// The tab may have navigated meanwhile: refresh when shown again.
window.addEventListener('focus', () => {
  if (!siteData.busy && !tokens.busy && !rules.busy && !utilities.busy) show();
});
show();
