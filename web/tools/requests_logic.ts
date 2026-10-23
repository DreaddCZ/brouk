/** Requests tool (006): filtering and Postman export, kept free of the DOM. */
import { isRecord } from '../shared/bridge.js';

export interface RequestSummary {
  readonly seq: number;
  readonly id: string;
  readonly url: string;
  readonly method: string;
  readonly type: string;
  readonly status: number;
  readonly mimeType: string;
  readonly finished: boolean;
  readonly error: string;
  readonly started: number;
  readonly duration: number;
  readonly size: number;
}

export interface RequestDetails extends RequestSummary {
  readonly requestHeaders: readonly (readonly [string, string])[];
  readonly responseHeaders: readonly (readonly [string, string])[];
  readonly postData?: string;
  readonly postDataTruncated?: boolean;
}

export function parseSummary(value: unknown): RequestSummary | null {
  if (!isRecord(value)) return null;
  const s = (k: string): string => (typeof value[k] === 'string' ? value[k] : '');
  const n = (k: string): number => (typeof value[k] === 'number' ? value[k] : 0);
  if (typeof value['seq'] !== 'number' || typeof value['url'] !== 'string') return null;
  return {
    seq: n('seq'), id: s('id'), url: s('url'), method: s('method'), type: s('type'), status: n('status'),
    mimeType: s('mimeType'), finished: value['finished'] === true, error: s('error'), started: n('started'),
    duration: typeof value['duration'] === 'number' ? value['duration'] : -1,
    size: typeof value['size'] === 'number' ? value['size'] : -1,
  };
}

function headerList(value: unknown): [string, string][] {
  if (!Array.isArray(value)) return [];
  const list: unknown[] = value;
  return list.flatMap((pair) =>
    Array.isArray(pair) && typeof pair[0] === 'string' && typeof pair[1] === 'string'
      ? [[pair[0], pair[1]] as [string, string]] : []);
}

export function parseDetails(value: unknown): RequestDetails | null {
  const summary = parseSummary(value);
  if (summary === null || !isRecord(value)) return null;
  const details: RequestDetails = {
    ...summary,
    requestHeaders: headerList(value['requestHeaders']),
    responseHeaders: headerList(value['responseHeaders']),
    ...(typeof value['postData'] === 'string' ? { postData: value['postData'] } : {}),
    ...(value['postDataTruncated'] === true ? { postDataTruncated: true } : {}),
  };
  return details;
}

export type TypeFilter = 'fetch' | 'all' | 'document' | 'script' | 'stylesheet' | 'image' | 'other';
export type StatusFilter = 'all' | '2xx' | '3xx' | '4xx' | '5xx' | 'failed';

export interface Filters {
  readonly type: TypeFilter;
  readonly method: string;  // '' for all
  readonly status: StatusFilter;
  readonly text: string;
}

const kTypes: Record<string, TypeFilter> = {
  Fetch: 'fetch', XHR: 'fetch', Document: 'document', Script: 'script', Stylesheet: 'stylesheet', Image: 'image',
};

export function matchesFilters(request: RequestSummary, filters: Filters): boolean {
  if (filters.type !== 'all' && (kTypes[request.type] ?? 'other') !== filters.type) return false;
  if (filters.method !== '' && request.method !== filters.method) return false;
  switch (filters.status) {
    case 'failed':
      if (request.error === '' && request.status < 400) return false;
      break;
    case 'all':
      break;
    default:
      if (Math.floor(request.status / 100) !== Number(filters.status[0])) return false;
  }
  const text = filters.text.trim().toLowerCase();
  return text === '' || request.url.toLowerCase().includes(text) || String(request.status).includes(text);
}

/** "GET /api/users" from method and URL. */
export function requestName(method: string, url: string): string {
  try {
    const parsed = new URL(url);
    return `${method} ${parsed.pathname}`;
  } catch {
    return `${method} ${url}`;
  }
}

/** A variable name from free text: "api.example.com:8443" -> "api_example_com_8443". */
export function variableName(text: string): string {
  return text.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '');
}

/** Secret header names from the setting ("Authorization, Cookie, ..."). */
export function parseSecretHeaders(setting: string): string[] {
  return setting.split(',').map((name) => name.trim().toLowerCase()).filter((name) => name !== '');
}

interface Variable {
  key: string;
  value: string;
  type: 'default' | 'secret';
}

export interface PostmanExport {
  /** Postman Collection v2.1: no secret values. */
  readonly collection: Record<string, unknown>;
  /** Postman environment: base URLs and secret values. */
  readonly environment: Record<string, unknown>;
}

/**
 * Selected requests as a Postman Collection v2.1, grouped in folders by
 * host. Each host becomes a {{baseUrl_<host>}} variable; values of secret
 * headers become variables too, and their values go only into the
 * environment. Base URLs are also collection variables, so the collection
 * works without the environment (with the secrets left to fill in).
 */
export function toPostman(requests: readonly RequestDetails[], name: string, secretHeaders: readonly string[]): PostmanExport {
  const secrets = new Set(secretHeaders.map((h) => h.toLowerCase()));
  const variables = new Map<string, Variable>();
  // Secret values by header, numbered when they differ.
  const secretValues = new Map<string, string>();
  const secretVariable = (header: string, value: string): string => {
    const base = variableName(header);
    for (let n = 1; ; ++n) {
      const key = n === 1 ? base : `${base}_${n}`;
      const known = secretValues.get(key);
      if (known === undefined) {
        secretValues.set(key, value);
        variables.set(key, { key, value, type: 'secret' });
        return key;
      }
      if (known === value) return key;
    }
  };
  const folders = new Map<string, Record<string, unknown>[]>();
  for (const request of requests) {
    let url: URL;
    try {
      url = new URL(request.url);
    } catch {
      continue;
    }
    const baseKey = `baseUrl_${variableName(url.host)}`;
    variables.set(baseKey, { key: baseKey, value: url.origin, type: 'default' });
    const headers = request.requestHeaders
      .filter(([header]) => !header.startsWith(':') && header.toLowerCase() !== 'content-length')
      .map(([header, value]) => ({
        key: header,
        value: secrets.has(header.toLowerCase()) ? `{{${secretVariable(header, value)}}}` : value,
      }));
    const path = url.pathname.split('/').filter((part) => part !== '');
    const query = [...url.searchParams.entries()].map(([key, value]) => ({ key, value }));
    const item: Record<string, unknown> = {
      name: requestName(request.method, request.url),
      request: {
        method: request.method,
        header: headers,
        url: {
          raw: `{{${baseKey}}}${url.pathname}${url.search}`,
          host: [`{{${baseKey}}}`],
          path,
          ...(query.length > 0 ? { query } : {}),
        },
        ...(request.postData !== undefined ? { body: postmanBody(request) } : {}),
      },
    };
    const folder = folders.get(url.host) ?? [];
    folder.push(item);
    folders.set(url.host, folder);
  }
  const collection = {
    info: {
      name,
      schema: 'https://schema.getpostman.com/json/collection/v2.1.0/collection.json',
    },
    item: [...folders.entries()].map(([host, items]) => ({ name: host, item: items })),
    variable: [...variables.values()]
      .filter((v) => v.type === 'default')
      .map((v) => ({ key: v.key, value: v.value })),
  };
  const environment = {
    name: `${name} environment`,
    values: [...variables.values()].map((v) => ({ key: v.key, value: v.value, type: v.type, enabled: true })),
    _postman_variable_scope: 'environment',
  };
  return { collection, environment };
}

function postmanBody(request: RequestDetails): Record<string, unknown> {
  const body = request.postData ?? '';
  const type = request.requestHeaders.find(([h]) => h.toLowerCase() === 'content-type')?.[1].toLowerCase() ?? '';
  if (type.includes('application/x-www-form-urlencoded')) {
    return {
      mode: 'urlencoded',
      urlencoded: [...new URLSearchParams(body).entries()].map(([key, value]) => ({ key, value })),
    };
  }
  if (type.includes('json')) {
    return { mode: 'raw', raw: body, options: { raw: { language: 'json' } } };
  }
  return { mode: 'raw', raw: body };
}

/** A file name from free text: "Example API" -> "Example API.postman_collection.json". */
export function exportFileNames(name: string): { collection: string; environment: string } {
  const safe = name.replace(/[\\/:*?"<>|]+/g, '-').trim() || 'Requests';
  return {
    collection: `${safe}.postman_collection.json`,
    environment: `${safe}.postman_environment.json`,
  };
}
