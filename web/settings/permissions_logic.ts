/** Logic of the site permissions list in settings (012). */
import type { PermissionSite } from './permissions_api.js';

/** "https://example.com" -> "example.com"; http stays visible, as in prompts. */
export function siteLabel(origin: string): string {
  return origin.startsWith('https://') ? origin.slice('https://'.length) : origin;
}

/** The sites whose address contains `query` (any case); all when it is empty. */
export function filterSites(sites: readonly PermissionSite[], query: string): PermissionSite[] {
  const needle = query.trim().toLowerCase();
  if (needle === '') return [...sites];
  return sites.filter((site) => site.origin.toLowerCase().includes(needle));
}
