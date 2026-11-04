/** The tab's environment overrides (027): what the page and the browser exchange. */
import { isRecord } from '../shared/bridge.js';

export interface EnvironmentLocation {
  readonly latitude: number;
  readonly longitude: number;
  readonly accuracy: number;
  /** A preset's name, e.g. "Tokyo"; '' for coordinates typed in. */
  readonly label: string;
}

/** The overrides; null or empty means "not changed". */
export interface TabEnvironment {
  readonly timeZone: string | null;
  readonly languages: readonly string[];
  readonly location: EnvironmentLocation | 'unavailable' | null;
  /** A preset id or custom text. */
  readonly userAgent: { readonly preset: string } | { readonly custom: string } | null;
  readonly colorScheme: 'light' | 'dark' | null;
  readonly reducedMotion: boolean;
}

export const kNoEnvironment: TabEnvironment = {
  timeZone: null, languages: [], location: null, userAgent: null, colorScheme: null, reducedMotion: false,
};

export interface EnvironmentState {
  readonly active: TabEnvironment;
  readonly presets: readonly { readonly id: string; readonly label: string }[];
}

/** Locations as Chrome's DevTools Sensors panel offers them, plus Prague. */
export const kLocationPresets: readonly EnvironmentLocation[] = [
  { label: 'Berlin', latitude: 52.520007, longitude: 13.404954, accuracy: 150 },
  { label: 'London', latitude: 51.507351, longitude: -0.127758, accuracy: 150 },
  { label: 'Moscow', latitude: 55.755826, longitude: 37.6173, accuracy: 150 },
  { label: 'Mumbai', latitude: 19.075984, longitude: 72.877656, accuracy: 150 },
  { label: 'Prague', latitude: 50.0755381, longitude: 14.4378005, accuracy: 150 },
  { label: 'San Francisco', latitude: 37.774929, longitude: -122.419416, accuracy: 150 },
  { label: 'Shanghai', latitude: 31.230416, longitude: 121.473701, accuracy: 150 },
  { label: 'São Paulo', latitude: -23.55052, longitude: -46.633309, accuracy: 150 },
  { label: 'Tokyo', latitude: 35.689487, longitude: 139.691706, accuracy: 150 },
];

/** "de-DE, de ,en" -> ["de-DE", "de", "en"]. */
export function parseLanguages(text: string): string[] {
  return text.split(/[,;\s]+/).map((l) => l.trim()).filter((l) => l !== '');
}

/** Whether every language looks like a language tag (en, de-DE, zh-Hant-TW). */
export function validLanguages(languages: readonly string[]): boolean {
  return languages.length > 0 && languages.every((l) => /^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$/.test(l));
}

export function environmentMessage(env: TabEnvironment): Record<string, unknown> {
  const message: Record<string, unknown> = {};
  if (env.timeZone !== null) message['timeZone'] = env.timeZone;
  if (env.languages.length > 0) message['languages'] = env.languages;
  if (env.location === 'unavailable') message['location'] = { unavailable: true };
  else if (env.location !== null) message['location'] = { ...env.location };
  if (env.userAgent !== null) {
    if ('preset' in env.userAgent) message['userAgentPreset'] = env.userAgent.preset;
    else message['userAgent'] = env.userAgent.custom;
  }
  if (env.colorScheme !== null) message['colorScheme'] = env.colorScheme;
  if (env.reducedMotion) message['reducedMotion'] = true;
  return message;
}

function parseEnvironment(value: unknown): TabEnvironment {
  if (!isRecord(value)) return kNoEnvironment;
  const location = value['location'];
  let parsedLocation: TabEnvironment['location'] = null;
  if (isRecord(location)) {
    if (location['unavailable'] === true) {
      parsedLocation = 'unavailable';
    } else if (typeof location['latitude'] === 'number' && typeof location['longitude'] === 'number' &&
      typeof location['accuracy'] === 'number') {
      parsedLocation = {
        latitude: location['latitude'], longitude: location['longitude'], accuracy: location['accuracy'],
        label: typeof location['label'] === 'string' ? location['label'] : '',
      };
    }
  }
  const languages = Array.isArray(value['languages'])
    ? (value['languages'] as unknown[]).filter((l): l is string => typeof l === 'string') : [];
  const preset = value['userAgentPreset'];
  const custom = value['userAgent'];
  const scheme = value['colorScheme'];
  return {
    timeZone: typeof value['timeZone'] === 'string' ? value['timeZone'] : null,
    languages,
    location: parsedLocation,
    userAgent: typeof preset === 'string' ? { preset } : typeof custom === 'string' ? { custom } : null,
    colorScheme: scheme === 'light' || scheme === 'dark' ? scheme : null,
    reducedMotion: value['reducedMotion'] === true,
  };
}

export function parseEnvironmentState(value: unknown): EnvironmentState {
  if (!isRecord(value)) return { active: kNoEnvironment, presets: [] };
  const presets = Array.isArray(value['presets']) ? (value['presets'] as unknown[]) : [];
  return {
    active: parseEnvironment(value['active']),
    presets: presets.flatMap((p) => (isRecord(p) && typeof p['id'] === 'string' && typeof p['label'] === 'string'
      ? [{ id: p['id'], label: p['label'] }] : [])),
  };
}

/** Whether any override is on. */
export function isChanged(env: TabEnvironment): boolean {
  return Object.keys(environmentMessage(env)).length > 0;
}
