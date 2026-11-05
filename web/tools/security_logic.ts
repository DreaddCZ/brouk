/**
 * Security check (029): passive checks of the page's headers, cookies and
 * elements, kept free of the DOM. Severities follow OWASP ZAP's passive
 * scan rules for the same issue (the rule's id is given with each finding);
 * checks ZAP has no rule for say so and are rated as noted.
 */
import type { Cookie } from './site_data_logic.js';
import { findJwts } from './tokens_logic.js';

export type Severity = 'high' | 'medium' | 'low' | 'info';
export const kSeverities: readonly Severity[] = ['high', 'medium', 'low', 'info'];

export type HeaderList = readonly (readonly [string, string])[];

export interface CheckedResponse {
  readonly url: string;
  readonly status: number;
  readonly requestHeaders: HeaderList;
  readonly responseHeaders: HeaderList;
}

export interface PageRequest {
  readonly url: string;
  readonly type: string;
  readonly error: string;
}

/** What the page has, read from its elements (null if they could not be read). */
export interface PageElements {
  /** Form actions as written ('' for none: the page itself). */
  readonly formActions: readonly string[];
  readonly passwordFields: number;
  /** Scripts and style sheets with their integrity attribute ('' if none). */
  readonly subresources: readonly { readonly url: string; readonly integrity: string }[];
  /** URLs of elements that load something (img, script, iframe…), as written. */
  readonly loads: readonly string[];
  /** Policies from <meta http-equiv="Content-Security-Policy">. */
  readonly metaPolicies: readonly string[];
}

export interface SecurityInput {
  readonly url: string;
  /** The page's site (registrable domain), from the browser. */
  readonly site: string;
  readonly document: CheckedResponse | null;
  /** Fetch/XHR responses of the page's own site. */
  readonly siteRequests: readonly CheckedResponse[];
  readonly pageRequests: readonly PageRequest[];
  readonly cookies: readonly Cookie[];
  readonly storage: readonly { readonly area: 'local' | 'session'; readonly key: string; readonly value: string }[];
  readonly elements: PageElements | null;
  /** Settings: comma-separated names. */
  readonly sessionCookies: string;
  readonly secretParameters: string;
  /** Seconds since 1970. */
  readonly now: number;
}

export interface Finding {
  /** Stable across checks, for hiding: "<check>" or "<check>:<subject>". */
  readonly id: string;
  readonly check: string;
  readonly severity: Severity;
  readonly title: string;
  /** What was found; secrets are shortened. */
  readonly found: string;
  readonly why: string;
  readonly fix: string;
  /** "ZAP rule 10038", or "No ZAP rule" with the rating's reason. */
  readonly rating: string;
  readonly link: string;
}

export interface CheckResult {
  readonly findings: Finding[];
  /** Titles of checks that ran and found nothing. */
  readonly passed: string[];
  /** Why some checks could not run (e.g. the page load was not recorded). */
  readonly notes: string[];
}

const kCheatSheets = 'https://cheatsheetseries.owasp.org/cheatsheets/';
const kLinks = {
  cookies: `${kCheatSheets}Session_Management_Cheat_Sheet.html#cookies`,
  csp: `${kCheatSheets}Content_Security_Policy_Cheat_Sheet.html`,
  hsts: `${kCheatSheets}HTTP_Strict_Transport_Security_Cheat_Sheet.html`,
  clickjacking: `${kCheatSheets}Clickjacking_Defense_Cheat_Sheet.html`,
  headers: `${kCheatSheets}HTTP_Headers_Cheat_Sheet.html`,
  cors: `${kCheatSheets}HTML5_Security_Cheat_Sheet.html#cross-origin-resource-sharing`,
  transport: `${kCheatSheets}Transport_Layer_Security_Cheat_Sheet.html`,
  caching: `${kCheatSheets}Session_Management_Cheat_Sheet.html#web-content-caching`,
  sri: `${kCheatSheets}Third_Party_Javascript_Management_Cheat_Sheet.html#subresource-integrity`,
  storage: `${kCheatSheets}HTML5_Security_Cheat_Sheet.html#local-storage`,
  urls: 'https://owasp.org/www-community/vulnerabilities/Information_exposure_through_query_strings_in_url',
} as const;

const kYear = 365 * 24 * 3600;
const kSessionCookieDays = 30;
/** How many example URLs a finding about many responses lists. */
const kExamples = 3;

// ---- Helpers ----

/** All values of a header (the protocol joins repeated headers with line breaks). */
export function headerValues(headers: HeaderList, name: string): string[] {
  const lower = name.toLowerCase();
  return headers
    .filter(([key]) => key.toLowerCase() === lower)
    .flatMap(([, value]) => value.split('\n'))
    .map((v) => v.trim())
    .filter((v) => v !== '');
}

function header(headers: HeaderList, name: string): string | null {
  return headerValues(headers, name)[0] ?? null;
}

/** "eyJhbGci…" — enough to recognise a value, too little to use it. */
export function shorten(value: string): string {
  return value.length > 12 ? `${value.slice(0, 8)}…` : value;
}

function list(text: string): string[] {
  return text.split(',').map((s) => s.trim()).filter((s) => s !== '');
}

/** Words of a name: "__Host-authToken" -> ["host", "auth", "token"]. */
function words(name: string): string[] {
  return name.replace(/([a-z0-9])([A-Z])/g, '$1 $2').toLowerCase().split(/[^a-z0-9]+/).filter((w) => w !== '');
}

/**
 * Whether a cookie looks like a session or token cookie: its value is a
 * JWT, or its name matches an entry of the setting — the whole name, one
 * of its words ("sid" in "my_sid"), or a prefix for entries ending in ".".
 */
export function isSessionCookie(name: string, value: string, names: string): boolean {
  if (findJwts(value).length > 0) return true;
  const lower = name.toLowerCase();
  const nameWords = words(name);
  return list(names).some((entry) => {
    const e = entry.toLowerCase();
    if (e.endsWith('.')) return lower.startsWith(e);
    if (lower === e) return true;
    return /^[a-z0-9]+$/.test(e) && nameWords.includes(e);
  });
}

function hostOf(url: string): string | null {
  try {
    return new URL(url).hostname.toLowerCase();
  } catch {
    return null;
  }
}

function schemeOf(url: string): string {
  try {
    return new URL(url).protocol;
  } catch {
    return '';
  }
}

/** Hosts Chrome treats as secure without HTTPS (local development). */
function isLocalHost(host: string): boolean {
  return host === 'localhost' || host.endsWith('.localhost') || host === '127.0.0.1' || host === '[::1]' ||
    /^127\.\d+\.\d+\.\d+$/.test(host);
}

function inSite(host: string, site: string): boolean {
  return site !== '' && (host === site || host.endsWith(`.${site}`));
}

function isInsecureUrl(url: string): boolean {
  const host = hostOf(url);
  return schemeOf(url) === 'http:' && host !== null && !isLocalHost(host);
}

// ---- Content-Security-Policy ----

export type Policy = ReadonlyMap<string, readonly string[]>;

/** Directives of one policy; later duplicates are ignored, as browsers do. */
export function parsePolicy(text: string): Policy {
  const policy = new Map<string, string[]>();
  for (const part of text.split(';')) {
    const [name, ...sources] = part.trim().split(/\s+/);
    if (name === undefined || name === '') continue;
    const key = name.toLowerCase();
    if (!policy.has(key)) policy.set(key, sources);
  }
  return policy;
}

/** The sources a directive uses, with fetch directives falling back to default-src. */
function sourcesOf(policy: Policy, directive: string): readonly string[] | null {
  const own = policy.get(directive);
  if (own !== undefined) return own;
  if (directive === 'script-src-elem' || directive === 'script-src-attr') return sourcesOf(policy, 'script-src');
  if (directive === 'style-src-elem' || directive === 'style-src-attr') return sourcesOf(policy, 'style-src');
  const fetchDirectives = ['script-src', 'style-src', 'object-src', 'img-src', 'connect-src', 'frame-src'];
  return fetchDirectives.includes(directive) ? (policy.get('default-src') ?? null) : null;
}

/** Whether inline code is allowed: 'unsafe-inline' without a nonce or hash (which disable it). */
function allowsInline(sources: readonly string[] | null): boolean {
  if (sources === null) return true;
  const lower = sources.map((s) => s.toLowerCase());
  return lower.includes("'unsafe-inline'") &&
    !lower.some((s) => s.startsWith("'nonce-") || s.startsWith("'sha") || s === "'strict-dynamic'");
}

function allowsAnyHost(sources: readonly string[] | null): boolean {
  if (sources === null) return true;
  if (sources.map((s) => s.toLowerCase()).includes("'strict-dynamic'")) return false;
  return sources.some((s) => s === '*' || s === 'http:' || s === 'https:' || s === 'data:' || /^http:\/\//i.test(s));
}

// ---- The checks ----

class Collector {
  readonly findings: Finding[] = [];
  readonly passed: string[] = [];
  readonly notes: string[] = [];

  /** Runs a check: its findings, or its title as passed. */
  check(title: string, found: Finding[]): void {
    if (found.length === 0) this.passed.push(title);
    else this.findings.push(...found);
  }
}

type Make = Omit<Finding, 'check'>;

function finding(check: string, make: Make): Finding {
  return { check, ...make };
}

/** "1 API response", "3 API responses". */
function count(n: number, one: string, many: string): string {
  return `${n} ${n === 1 ? one : many}`;
}

function examples(urls: readonly string[]): string {
  const shown = urls.slice(0, kExamples).join(', ');
  return urls.length > kExamples ? `${shown} and ${urls.length - kExamples} more` : shown;
}

function checkCookies(input: SecurityInput, out: Collector, https: boolean, pageHost: string): void {
  const session = input.cookies.filter((c) => isSessionCookie(c.name, c.value, input.sessionCookies));
  if (https) {
    out.check('Cookies are sent only over HTTPS (Secure)', input.cookies.filter((c) => !c.secure).map((c) =>
      finding('cookie-secure', {
        id: `cookie-secure:${c.name}`, severity: 'low', title: `Cookie ${c.name} without Secure`,
        found: `${c.name} (domain ${c.domain}) can also be sent over plain HTTP.`,
        why: 'Without Secure, the browser sends the cookie over an unencrypted connection too, where it can be read.',
        fix: 'Add the Secure attribute when setting the cookie.',
        rating: 'ZAP rule 10011', link: kLinks.cookies,
      })));
  }
  out.check('Session cookies are not readable by scripts (HttpOnly)', session.filter((c) => !c.httpOnly).map((c) =>
    finding('cookie-httponly', {
      id: `cookie-httponly:${c.name}`, severity: 'low', title: `Session cookie ${c.name} without HttpOnly`,
      found: `${c.name} = ${shorten(c.value)} is readable by any script on the page.`,
      why: 'A script injected through cross-site scripting can read the cookie and take over the session.',
      fix: 'Add the HttpOnly attribute, unless the page’s own scripts must read it.',
      rating: 'ZAP rule 10010', link: kLinks.cookies,
    })));
  out.check('Cookies set SameSite', input.cookies.filter((c) => c.sameSite === '' || (c.sameSite === 'None' && !c.secure))
    .map((c) => finding('cookie-samesite', {
      id: `cookie-samesite:${c.name}`, severity: 'low',
      title: c.sameSite === '' ? `Cookie ${c.name} without SameSite` : `Cookie ${c.name} with SameSite=None but not Secure`,
      found: c.sameSite === ''
        ? `${c.name} has no SameSite attribute (browsers then use Lax, older ones none).`
        : `${c.name} has SameSite=None without Secure, which browsers reject or treat as Lax.`,
      why: 'SameSite limits sending the cookie with requests started by other sites (cross-site request forgery).',
      fix: 'Set SameSite=Lax or Strict; use None only when needed, and then with Secure.',
      rating: 'ZAP rule 10054', link: kLinks.cookies,
    })));
  out.check('Session cookies are scoped to this host', session.filter((c) => {
    const domain = c.domain.replace(/^\./, '').toLowerCase();
    return c.domain.startsWith('.') && domain !== pageHost;
  }).map((c) => finding('cookie-scope', {
    id: `cookie-scope:${c.name}`, severity: 'info', title: `Session cookie ${c.name} shared with other subdomains`,
    found: `${c.name} is set for ${c.domain}, so every subdomain of ${c.domain.replace(/^\./, '')} receives it.`,
    why: 'Any subdomain, including a less trusted or compromised one, gets the session cookie.',
    fix: 'Leave out the Domain attribute (or use the __Host- prefix) so only this host gets the cookie.',
    rating: 'ZAP rule 90033', link: kLinks.cookies,
  })));
  out.check('Session cookies are short-lived', session.filter((c) => c.expires > 0 &&
    c.expires - input.now > kSessionCookieDays * 24 * 3600).map((c) => finding('cookie-lifetime', {
    id: `cookie-lifetime:${c.name}`, severity: 'info', title: `Session cookie ${c.name} lives long`,
    found: `${c.name} expires in ${Math.round((c.expires - input.now) / 86400)} days.`,
    why: 'A long-lived session cookie stays usable long after the user stopped working, e.g. on a shared computer.',
    fix: `Let the session expire after hours or days (over ${kSessionCookieDays} days is reported), or end it on the server.`,
    rating: 'No ZAP rule; Informational, as ZAP rates cookie scope (90033)', link: kLinks.cookies,
  })));
  // Prefixed cookies the browser refused (they never reach the cookie list).
  const responses = [...(input.document ? [input.document] : []), ...input.siteRequests];
  const prefixProblems = new Map<string, string>();
  for (const response of responses) {
    for (const line of headerValues(response.responseHeaders, 'set-cookie')) {
      const problem = prefixProblem(line);
      if (problem !== null) prefixProblems.set(problem.name, problem.problem);
    }
  }
  out.check('Cookie prefixes (__Host-, __Secure-) are used correctly', [...prefixProblems].map(([name, problem]) =>
    finding('cookie-prefix', {
      id: `cookie-prefix:${name}`, severity: 'low', title: `Cookie ${name} breaks its prefix`,
      found: `${name}: ${problem} The browser refuses the cookie.`,
      why: 'Browsers reject prefixed cookies that do not meet the prefix’s rules, so the site does not get the cookie.',
      fix: '__Host- needs Secure, Path=/ and no Domain; __Secure- needs Secure.',
      rating: 'No ZAP rule; Low, as ZAP rates a cookie without Secure (10011)', link: kLinks.cookies,
    })));
}

/** For a Set-Cookie line: a prefixed cookie that does not meet its prefix's rules. */
export function prefixProblem(line: string): { name: string; problem: string } | null {
  const [pair, ...attributes] = line.split(';');
  const name = (pair ?? '').split('=')[0]?.trim() ?? '';
  const host = name.startsWith('__Host-');
  if (!host && !name.startsWith('__Secure-')) return null;
  const attrs = new Map(attributes.map((a) => {
    const [key, ...value] = a.split('=');
    return [(key ?? '').trim().toLowerCase(), value.join('=').trim()] as const;
  }));
  if (!attrs.has('secure')) return { name, problem: 'no Secure attribute.' };
  if (host && attrs.has('domain')) return { name, problem: '__Host- cookies must not set Domain.' };
  if (host && attrs.get('path') !== '/') return { name, problem: '__Host- cookies need Path=/.' };
  return null;
}

function checkPolicy(document: CheckedResponse, input: SecurityInput, out: Collector): void {
  const enforced = [...headerValues(document.responseHeaders, 'content-security-policy'),
    ...(input.elements?.metaPolicies ?? [])].map(parsePolicy);
  const reportOnly = headerValues(document.responseHeaders, 'content-security-policy-report-only');
  out.check('A Content-Security-Policy is set', enforced.length > 0 ? [] : [finding('csp-missing', {
    id: 'csp-missing', severity: 'medium', title: 'No Content-Security-Policy',
    found: 'The page sets no Content-Security-Policy header (or <meta> policy).',
    why: 'A policy limits which scripts can run, which makes cross-site scripting much harder to exploit.',
    fix: 'Send a Content-Security-Policy, starting with e.g. default-src \'self\'; object-src \'none\'; base-uri \'self\'; frame-ancestors \'self\'.',
    rating: 'ZAP rule 10038', link: kLinks.csp,
  })]);
  if (reportOnly.length > 0) {
    out.findings.push(finding('csp-report-only', {
      id: 'csp-report-only', severity: 'info', title: 'Content-Security-Policy only reports',
      found: `Content-Security-Policy-Report-Only: ${reportOnly[0] ?? ''}`,
      why: 'A report-only policy blocks nothing; it only reports what it would block.',
      fix: 'Once the reports are clean, send the policy as Content-Security-Policy.',
      rating: 'ZAP rule 10038 (report-only)', link: kLinks.csp,
    }));
  }
  if (enforced.length === 0) return;
  // Several policies all apply: something is allowed only if every one allows it.
  const all = (allows: (policy: Policy) => boolean): boolean => enforced.every(allows);
  const weak: Finding[] = [];
  const weakness = (key: string, title: string, found: string, fix: string): void => {
    weak.push(finding('csp-weak', {
      id: `csp-weak:${key}`, severity: 'medium', title, found,
      why: 'This part of the policy lets injected code run, so the policy does not protect against cross-site scripting.',
      fix, rating: 'ZAP rule 10055', link: kLinks.csp,
    }));
  };
  if (all((p) => allowsInline(sourcesOf(p, 'script-src')))) {
    weakness('script-inline', "Policy allows inline scripts ('unsafe-inline')", 'script-src (or default-src) allows inline scripts, or the policy has no script-src at all.',
      'Remove \'unsafe-inline\' from script-src; use nonces or hashes for the inline scripts you need.');
  }
  if (all((p) => (sourcesOf(p, 'script-src') ?? ["'unsafe-eval'"]).some((s) => s.toLowerCase() === "'unsafe-eval'"))) {
    weakness('script-eval', "Policy allows eval ('unsafe-eval')", 'script-src (or default-src) allows eval() and similar, or there is no script-src.',
      'Remove \'unsafe-eval\'; replace eval() and new Function() in the code.');
  }
  if (all((p) => allowsAnyHost(sourcesOf(p, 'script-src')))) {
    weakness('script-wildcard', 'Policy allows scripts from any host', 'script-src (or default-src) has *, http:, https: or data:, or there is no script-src.',
      'List the hosts scripts come from, or use \'strict-dynamic\' with nonces.');
  }
  if (all((p) => allowsInline(sourcesOf(p, 'style-src')))) {
    weakness('style-inline', "Policy allows inline styles ('unsafe-inline')", 'style-src (or default-src) allows inline styles, or there is no style-src.',
      'Remove \'unsafe-inline\' from style-src, with nonces or hashes for the styles you need.');
  }
  out.check('The Content-Security-Policy blocks inline and foreign scripts', weak);
  const missing = ['base-uri', 'form-action', 'frame-ancestors'].filter((d) => all((p) => !p.has(d)));
  if (all((p) => sourcesOf(p, 'object-src') === null)) missing.push('object-src');
  out.check('The Content-Security-Policy restricts plugins, base URL, forms and framing', missing.map((d) =>
    finding('csp-directive', {
      id: `csp-directive:${d}`, severity: 'medium', title: `Policy does not set ${d}`,
      found: d === 'object-src'
        ? 'Neither object-src nor default-src is set, so plugins (<object>, <embed>) load from anywhere.'
        : `${d} is not set; it does not fall back to default-src, so it allows everything.`,
      why: d === 'base-uri' ? 'An injected <base> tag can redirect the page’s relative script URLs.'
        : d === 'form-action' ? 'Injected forms can send their data anywhere.'
        : d === 'frame-ancestors' ? 'Other sites can embed the page in a frame (clickjacking).'
        : 'Plugin content can run scripts that bypass the policy.',
      fix: d === 'object-src' ? "Add object-src 'none'." : d === 'frame-ancestors' ? "Add frame-ancestors 'self' (or 'none')." : `Add ${d} 'self'.`,
      rating: 'ZAP rule 10055', link: kLinks.csp,
    })));
}

function checkHeaders(document: CheckedResponse, input: SecurityInput, out: Collector, https: boolean): void {
  const headers = document.responseHeaders;
  if (https) {
    const hsts = header(headers, 'strict-transport-security');
    const maxAge = hsts === null ? null : Number(/max-age\s*=\s*"?(\d+)/i.exec(hsts)?.[1] ?? 0);
    out.check('Strict-Transport-Security is set for at least a year', hsts === null ? [finding('hsts', {
      id: 'hsts-missing', severity: 'low', title: 'No Strict-Transport-Security',
      found: 'The page is served over HTTPS without a Strict-Transport-Security header.',
      why: 'Without it, a first visit or a typed http:// address can be intercepted before the redirect to HTTPS.',
      fix: 'Send Strict-Transport-Security: max-age=31536000; includeSubDomains.',
      rating: 'ZAP rule 10035', link: kLinks.hsts,
    })] : (maxAge ?? 0) < kYear ? [finding('hsts', {
      id: 'hsts-short', severity: 'low', title: 'Strict-Transport-Security for less than a year',
      found: `Strict-Transport-Security: ${hsts}`,
      why: 'A short max-age (0 switches it off) leaves returning visitors unprotected once it expires.',
      fix: 'Use max-age=31536000 (one year) or more.',
      rating: 'ZAP rule 10035', link: kLinks.hsts,
    })] : []);
  }
  const frameOptions = (header(headers, 'x-frame-options') ?? '').toUpperCase();
  const policies = [...headerValues(headers, 'content-security-policy'), ...(input.elements?.metaPolicies ?? [])]
    .map(parsePolicy);
  const frameAncestors = policies.some((p) => p.has('frame-ancestors'));
  out.check('Other sites cannot frame the page (clickjacking)',
    frameAncestors || frameOptions === 'DENY' || frameOptions === 'SAMEORIGIN' ? [] : [finding('clickjacking', {
      id: 'clickjacking', severity: 'medium', title: 'No protection against clickjacking',
      found: frameOptions === '' ? 'Neither X-Frame-Options nor CSP frame-ancestors is set.' : `X-Frame-Options: ${frameOptions} (not DENY or SAMEORIGIN), and no frame-ancestors.`,
      why: 'Another site can show the page in an invisible frame and trick the user into clicking on it.',
      fix: "Send Content-Security-Policy: frame-ancestors 'self' (and X-Frame-Options: SAMEORIGIN for old browsers).",
      rating: 'ZAP rule 10020', link: kLinks.clickjacking,
    })]);
  const sniffing = [document, ...input.siteRequests]
    .filter((r) => (header(r.responseHeaders, 'x-content-type-options') ?? '').toLowerCase() !== 'nosniff');
  const documentSniffs = sniffing.includes(document);
  const apiSniffs = sniffing.filter((r) => r !== document);
  out.check('Responses forbid content sniffing (nosniff)', [
    ...(documentSniffs ? [finding('nosniff', {
      id: 'nosniff:document', severity: 'low', title: 'Page without X-Content-Type-Options: nosniff',
      found: 'The page’s response has no X-Content-Type-Options: nosniff.',
      why: 'Browsers may guess a response’s type and run it as script or HTML when it is not.',
      fix: 'Send X-Content-Type-Options: nosniff with every response.',
      rating: 'ZAP rule 10021', link: `${kLinks.headers}#x-content-type-options`,
    })] : []),
    ...(apiSniffs.length > 0 ? [finding('nosniff', {
      id: 'nosniff:api', severity: 'low', title: count(apiSniffs.length, 'API response', 'API responses') + ' without nosniff',
      found: `For example ${examples(apiSniffs.map((r) => r.url))}.`,
      why: 'Browsers may guess a response’s type and run it as script or HTML when it is not.',
      fix: 'Send X-Content-Type-Options: nosniff with every response, API responses included.',
      rating: 'ZAP rule 10021', link: `${kLinks.headers}#x-content-type-options`,
    })] : []),
  ]);
  const referrer = (header(headers, 'referrer-policy') ?? '').toLowerCase();
  out.check('Referrer-Policy is set', referrer !== '' && referrer !== 'unsafe-url' ? [] : [finding('referrer-policy', {
    id: 'referrer-policy', severity: 'info',
    title: referrer === '' ? 'No Referrer-Policy' : 'Referrer-Policy sends full URLs to other sites',
    found: referrer === '' ? 'No Referrer-Policy header (browsers then use strict-origin-when-cross-origin).' : 'Referrer-Policy: unsafe-url',
    why: 'Full page addresses, which may contain ids or tokens, can be sent to other sites in the Referer header.',
    fix: 'Send Referrer-Policy: strict-origin-when-cross-origin (or stricter).',
    rating: 'No ZAP rule; Informational, as browsers have a safe default', link: `${kLinks.headers}#referrer-policy`,
  })]);
  out.check('Cross-Origin-Opener-Policy is set', header(headers, 'cross-origin-opener-policy') !== null ? [] : [
    finding('coop', {
      id: 'coop', severity: 'low', title: 'No Cross-Origin-Opener-Policy',
      found: 'The page sends no Cross-Origin-Opener-Policy.',
      why: 'Windows the page opens (or that open it) share a browsing context group with it, which helps cross-window and Spectre-style attacks.',
      fix: 'Send Cross-Origin-Opener-Policy: same-origin (or same-origin-allow-popups for sign-in popups).',
      rating: 'ZAP rule 90004', link: `${kLinks.headers}#cross-origin-opener-policy-coop`,
    })]);
  out.check('Permissions-Policy is set', header(headers, 'permissions-policy') !== null ? [] : [
    finding('permissions-policy', {
      id: 'permissions-policy', severity: 'low', title: 'No Permissions-Policy',
      found: 'The page sends no Permissions-Policy.',
      why: 'Without it, embedded content and injected scripts may ask for the camera, location and other features.',
      fix: 'Send a Permissions-Policy that turns off what the site does not use, e.g. camera=(), geolocation=().',
      rating: 'ZAP rule 10063', link: `${kLinks.headers}#permissions-policy`,
    })]);
  // Version disclosure, on the page and the site's API responses.
  const responses = [document, ...input.siteRequests];
  const leaks: Finding[] = [];
  const leak = (name: string, rule: string, test: (value: string) => boolean): void => {
    for (const response of responses) {
      const value = header(response.responseHeaders, name);
      if (value !== null && test(value)) {
        leaks.push(finding('version', {
          id: `version:${name.toLowerCase()}`, severity: 'low', title: `${name} header tells what the server runs`,
          found: `${name}: ${value} (${response.url})`,
          why: 'Knowing the software and its version makes it easier to look up known vulnerabilities to attack.',
          fix: `Remove the ${name} header, or at least its version.`,
          rating: `ZAP rule ${rule}`, link: `${kLinks.headers}#${name.toLowerCase()}`,
        }));
        return;
      }
    }
  };
  leak('Server', '10036', (v) => /\d/.test(v));
  leak('X-Powered-By', '10037', () => true);
  leak('X-AspNet-Version', '10061', () => true);
  leak('X-AspNetMvc-Version', '10061', () => true);
  out.check('Server software versions are not disclosed', leaks);
}

function checkApis(input: SecurityInput, out: Collector): void {
  const cors: Finding[] = [];
  const seen = new Set<string>();
  for (const response of input.siteRequests) {
    const allowed = header(response.responseHeaders, 'access-control-allow-origin');
    const credentials = (header(response.responseHeaders, 'access-control-allow-credentials') ?? '').toLowerCase() === 'true';
    const origin = header(response.requestHeaders, 'origin');
    if (allowed === null || !credentials) continue;
    const kind = allowed === '*' ? 'wildcard' : allowed === 'null' ? 'null' : origin !== null && allowed === origin ? 'echo' : '';
    if (kind === '' || seen.has(kind)) continue;
    seen.add(kind);
    cors.push(finding('cors', {
      id: `cors:${kind}`,
      severity: kind === 'null' ? 'high' : kind === 'wildcard' ? 'medium' : 'info',
      title: kind === 'echo' ? 'API allows the requesting origin with credentials'
        : `API allows origin ${allowed} with credentials`,
      found: `Access-Control-Allow-Origin: ${allowed} with Access-Control-Allow-Credentials: true (${response.url})`,
      why: kind === 'echo'
        ? 'Fine if the server allows only known origins; if it echoes any Origin, every site can read the API as the signed-in user. A passive check cannot tell the two apart.'
        : kind === 'null'
          ? 'Sandboxed frames and local files send Origin: null, so any site can read the API as the signed-in user.'
          : 'Browsers refuse * with credentials, which suggests the server’s CORS set-up is not what was intended.',
      fix: kind === 'echo'
        ? 'Check that the server compares Origin with a list of allowed origins (e.g. send another Origin with a request rule).'
        : 'Allow only a list of known origins.',
      rating: kind === 'echo' ? 'No passive ZAP rule (40040 is active); Informational until confirmed'
        : kind === 'null' ? 'ZAP rule 40040' : 'ZAP rule 10098',
      link: kLinks.cors,
    }));
  }
  out.check('API responses allow credentials only for known origins', cors);
  const cached = input.siteRequests.filter((r) => r.status >= 200 && r.status < 300 && isAuthenticated(r, input) &&
    !/no-store/i.test(header(r.responseHeaders, 'cache-control') ?? ''));
  out.check('Responses to signed-in requests are not cached', cached.length === 0 ? [] : [finding('cache', {
    id: 'cache-authenticated', severity: 'info', title: count(cached.length, 'signed-in response', 'signed-in responses') + ' may be cached',
    found: `Without Cache-Control: no-store, for example ${examples(cached.map((r) => r.url))}.`,
    why: 'Personal data may stay in the browser’s or a proxy’s cache, where the next user of the computer can find it.',
    fix: 'Send Cache-Control: no-store with responses that contain personal or sensitive data.',
    rating: 'ZAP rule 10015', link: kLinks.caching,
  })]);
}

function isAuthenticated(response: CheckedResponse, input: SecurityInput): boolean {
  if (header(response.requestHeaders, 'authorization') !== null) return true;
  const cookies = header(response.requestHeaders, 'cookie') ?? '';
  return cookies.split(';').some((pair) => {
    const [name, ...value] = pair.split('=');
    return isSessionCookie((name ?? '').trim(), value.join('=').trim(), input.sessionCookies);
  });
}

function checkTransport(input: SecurityInput, out: Collector, https: boolean, pageHost: string): void {
  const local = isLocalHost(pageHost);
  if (!https && !local) {
    out.findings.push(finding('http-page', {
      id: 'http-page', severity: 'medium', title: 'Page served over plain HTTP',
      found: `${input.url} is not encrypted.`,
      why: 'Anyone on the network can read and change the page and everything sent to it.',
      fix: 'Serve the site only over HTTPS and redirect HTTP to it.',
      rating: 'ZAP rule 10106', link: kLinks.transport,
    }));
    if ((input.elements?.passwordFields ?? 0) > 0) {
      out.findings.push(finding('password-http', {
        id: 'password-http', severity: 'medium', title: 'Password field on an HTTP page',
        found: 'The page asks for a password over an unencrypted connection.',
        why: 'The password can be read on the network, and the page itself can be changed to steal it.',
        fix: 'Serve the page over HTTPS.',
        rating: 'No ZAP rule; Medium, as ZAP rates an HTTP-only site (10106)', link: kLinks.transport,
      }));
    }
    return;
  }
  if (!https) return;  // A local development page: Chrome treats it as secure.
  const insecure = [...new Set([
    ...input.pageRequests.map((r) => r.url),
    ...(input.elements?.loads ?? []),
  ].filter(isInsecureUrl))];
  out.check('Everything on the page is loaded over HTTPS', insecure.length === 0 ? [] : [finding('mixed-content', {
    id: 'mixed-content', severity: 'medium', title: insecure.length === 1 ? '1 part of the page uses plain HTTP' : `${insecure.length} parts of the page use plain HTTP`,
    found: `Mixed content (loaded or blocked by the browser): ${examples(insecure)}.`,
    why: 'Parts loaded over HTTP can be read or replaced on the network; scripts replaced that way control the page.',
    fix: 'Load everything over HTTPS (or add upgrade-insecure-requests to the policy).',
    rating: 'ZAP rule 10040', link: kLinks.transport,
  })]);
  const forms = (input.elements?.formActions ?? []).filter((action) => {
    try {
      return isInsecureUrl(new URL(action, input.url).href);
    } catch {
      return false;
    }
  });
  if (input.elements !== null) {
    out.check('Forms send to HTTPS', forms.length === 0 ? [] : [finding('form-http', {
      id: 'form-http', severity: 'medium', title: 'A form sends its data over plain HTTP',
      found: `Form action ${examples(forms)}.`,
      why: 'What the user types (perhaps a password) leaves the encrypted connection.',
      fix: 'Point the form to an https:// address.',
      rating: 'ZAP rule 10041', link: kLinks.transport,
    })]);
  }
}

/** Whether a secret parameter is a session id or token (Medium in ZAP) or other sensitive data (Info). */
function isSessionParameter(name: string): boolean {
  return /token|sess|sid/i.test(name);
}

function checkUrls(input: SecurityInput, out: Collector): void {
  const names = new Set(list(input.secretParameters).map((n) => n.toLowerCase()));
  const found = new Map<string, string>();
  const scan = (url: string): void => {
    let parsed: URL;
    try {
      parsed = new URL(url);
    } catch {
      return;
    }
    // The fragment too: implicit sign-in flows put tokens there, and it stays in the history.
    const params = [...parsed.searchParams, ...new URLSearchParams(parsed.hash.slice(1))];
    for (const [name, value] of params) {
      if (value !== '' && names.has(name.toLowerCase()) && !found.has(name)) {
        found.set(name, `${parsed.origin}${parsed.pathname}?${name}=${shorten(value)}`);
      }
    }
  };
  scan(input.url);
  for (const request of input.pageRequests) scan(request.url);
  out.check('URLs carry no tokens or passwords', [...found].map(([name, where]) => finding('url-secret', {
    id: `url-secret:${name.toLowerCase()}`, severity: isSessionParameter(name) ? 'medium' : 'info',
    title: `${name} in a URL`,
    found: where,
    why: 'URLs end up in server and proxy logs, the browser history and Referer headers, where others can read them.',
    fix: 'Send secrets in a header (Authorization) or the request body, never in the URL.',
    rating: isSessionParameter(name) ? 'ZAP rule 3 (session ID in URL)' : 'ZAP rule 10024',
    link: kLinks.urls,
  })));
}

function checkPage(input: SecurityInput, out: Collector): void {
  if (input.elements !== null) {
    const foreign = input.elements.subresources.filter((r) => {
      const host = hostOf(r.url);
      return host !== null && !inSite(host, input.site) && r.integrity === '';
    }).map((r) => r.url);
    out.check('Scripts and styles from other sites are pinned (Subresource Integrity)', foreign.length === 0 ? [] : [
      finding('sri', {
        id: 'sri', severity: 'medium', title: count(foreign.length, 'script or style', 'scripts or styles') + ' from other sites without integrity',
        found: `For example ${examples(foreign)}.`,
        why: 'If the other site (often a CDN) is compromised, its changed file runs in this page.',
        fix: 'Add integrity="sha384-…" and crossorigin="anonymous" to <script> and <link> tags loading from other sites.',
        rating: 'ZAP rule 90003', link: kLinks.sri,
      })]);
  }
  const stored = input.storage.filter((item) => findJwts(item.value).length > 0);
  out.check('No tokens in local or session storage', stored.map((item) => finding('jwt-storage', {
    id: `jwt-storage:${item.area}:${item.key}`, severity: 'info',
    title: `JWT in ${item.area} storage (${item.key})`,
    found: `${item.area}Storage["${item.key}"] = ${shorten(item.value)}`,
    why: 'Any script on the page, including injected ones, can read the storage and use the token elsewhere.',
    fix: 'Keep tokens in memory or in an HttpOnly cookie where possible; keep their lifetime short.',
    rating: 'No ZAP rule; Informational, as storing tokens is a common, deliberate choice', link: kLinks.storage,
  })));
}

/** Runs every check that the input allows. */
export function runSecurityChecks(input: SecurityInput): CheckResult {
  const out = new Collector();
  const https = schemeOf(input.url) === 'https:';
  const pageHost = hostOf(input.url) ?? '';
  // A development copy on this computer is checked as it will be served
  // (over HTTPS), so missing Secure flags and HSTS show before release.
  const local = !https && isLocalHost(pageHost);
  if (local) out.notes.push(`${pageHost} is checked as if it were served over HTTPS (Secure cookies, HSTS).`);
  checkTransport(input, out, https, pageHost);
  checkCookies(input, out, https || local, pageHost);
  if (input.document !== null) {
    checkPolicy(input.document, input, out);
    checkHeaders(input.document, input, out, https || local);
  } else {
    out.notes.push('The page’s own response was not recorded (the dev tools were turned on after it loaded), so its headers were not checked. Reload and check.');
  }
  checkApis(input, out);
  checkUrls(input, out);
  checkPage(input, out);
  if (input.elements === null) out.notes.push('The page’s elements could not be read, so forms and scripts were not checked.');
  const order = (s: Severity): number => kSeverities.indexOf(s);
  out.findings.sort((a, b) => order(a.severity) - order(b.severity));
  return { findings: out.findings, passed: out.passed, notes: out.notes };
}

/** "2 high, 3 medium, 5 passed". */
export function summary(result: CheckResult, hidden: ReadonlySet<string>): string {
  const shown = result.findings.filter((f) => !hidden.has(f.id));
  const parts = kSeverities.flatMap((s) => {
    const n = shown.filter((f) => f.severity === s).length;
    return n > 0 ? [`${n} ${s === 'info' ? 'informational' : s}`] : [];
  });
  parts.push(`${result.passed.length} passed`);
  const hiddenCount = result.findings.length - shown.length;
  if (hiddenCount > 0) parts.push(`${hiddenCount} hidden`);
  return parts.join(', ');
}

const kSeverityNames: Record<Severity, string> = { high: 'High', medium: 'Medium', low: 'Low', info: 'Informational' };

export function severityName(severity: Severity): string {
  return kSeverityNames[severity];
}

/** The findings as Markdown for a bug report (hidden ones left out). */
export function findingsMarkdown(result: CheckResult, url: string, hidden: ReadonlySet<string>, checkedAt: Date): string {
  const shown = result.findings.filter((f) => !hidden.has(f.id));
  const lines = [`# Security check: ${url}`, '', `Checked ${checkedAt.toISOString()} with Brouk's developer tools (passive checks, ratings as in OWASP ZAP).`, ''];
  lines.push(`Summary: ${summary(result, hidden)}.`, '');
  for (const severity of kSeverities) {
    const group = shown.filter((f) => f.severity === severity);
    if (group.length === 0) continue;
    lines.push(`## ${severityName(severity)}`, '');
    for (const f of group) {
      lines.push(`- **${f.title}** — ${f.found}`, `  - Why: ${f.why}`, `  - Fix: ${f.fix}`, `  - ${f.rating}; ${f.link}`);
    }
    lines.push('');
  }
  if (result.notes.length > 0) lines.push(...result.notes.map((n) => `_${n}_`), '');
  return lines.join('\n');
}
