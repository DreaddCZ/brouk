/** Unix time (027): timestamps to dates and back, in any time zone. */

export type TimeUnit = 's' | 'ms' | 'us';

/** The unit of a timestamp from its size: seconds up to 11 digits, then milliseconds, then microseconds. */
export function detectUnit(value: number): TimeUnit {
  const digits = Math.trunc(Math.abs(value)).toString().length;
  if (digits <= 11) return 's';
  if (digits <= 14) return 'ms';
  return 'us';
}

/** Milliseconds since 1970 of a timestamp, or null if it is not a number. */
export function parseTimestamp(text: string, unit: TimeUnit | 'auto'): { ms: number; unit: TimeUnit } | null {
  const trimmed = text.trim();
  if (!/^-?\d+(\.\d+)?$/.test(trimmed)) return null;
  const value = Number(trimmed);
  const used = unit === 'auto' ? detectUnit(value) : unit;
  const ms = used === 's' ? value * 1000 : used === 'ms' ? value : value / 1000;
  // The range of a JavaScript date.
  if (!Number.isFinite(ms) || Math.abs(ms) > 8.64e15) return null;
  return { ms, unit: used };
}

/** "2024-10-27 03:33:20" in `timeZone` (an IANA name). */
export function formatInZone(ms: number, timeZone: string): string {
  const parts = zoneParts(ms, timeZone);
  const pad = (n: number, width = 2): string => String(n).padStart(width, '0');
  const millis = ms % 1000 === 0 ? '' : `.${pad(((ms % 1000) + 1000) % 1000, 3)}`;
  return `${pad(parts.year, 4)}-${pad(parts.month)}-${pad(parts.day)} ${pad(parts.hour)}:${pad(parts.minute)}:${pad(parts.second)}${millis}`;
}

/** The offset of `timeZone` from UTC at that moment, as "+01:00". */
export function zoneOffsetText(ms: number, timeZone: string): string {
  const offset = zoneOffsetMinutes(ms, timeZone);
  const sign = offset < 0 ? '-' : '+';
  const abs = Math.abs(offset);
  return `${sign}${String(Math.floor(abs / 60)).padStart(2, '0')}:${String(abs % 60).padStart(2, '0')}`;
}

interface ZoneParts {
  year: number;
  month: number;
  day: number;
  hour: number;
  minute: number;
  second: number;
}

function zoneParts(ms: number, timeZone: string): ZoneParts {
  const format = new Intl.DateTimeFormat('en-US', {
    timeZone,
    hourCycle: 'h23',
    year: 'numeric',
    month: 'numeric',
    day: 'numeric',
    hour: 'numeric',
    minute: 'numeric',
    second: 'numeric',
    era: 'short',
  });
  const value = (type: string): number => Number(format.formatToParts(ms).find((p) => p.type === type)?.value ?? 0);
  const era = format.formatToParts(ms).find((p) => p.type === 'era')?.value ?? 'AD';
  const year = value('year');
  return {
    year: era.startsWith('B') ? 1 - year : year,
    month: value('month'),
    day: value('day'),
    hour: value('hour'),
    minute: value('minute'),
    second: value('second'),
  };
}

/** Minutes `timeZone` is ahead of UTC at that moment. */
export function zoneOffsetMinutes(ms: number, timeZone: string): number {
  const p = zoneParts(ms, timeZone);
  const asUtc = Date.UTC(p.year, p.month - 1, p.day, p.hour, p.minute, p.second);
  return Math.round((asUtc - (ms - (((ms % 1000) + 1000) % 1000))) / 60000);
}

/** Whether `name` is a time zone this browser knows. */
export function isTimeZone(name: string): boolean {
  try {
    new Intl.DateTimeFormat('en-US', { timeZone: name });
    return true;
  } catch {
    return false;
  }
}

/** "3 hours ago", "in 12 minutes", relative to `now`. */
export function relativeTime(ms: number, now: number): string {
  const seconds = (ms - now) / 1000;
  const units: [Intl.RelativeTimeFormatUnit, number][] = [
    ['year', 365 * 24 * 3600],
    ['month', 30 * 24 * 3600],
    ['day', 24 * 3600],
    ['hour', 3600],
    ['minute', 60],
    ['second', 1],
  ];
  const format = new Intl.RelativeTimeFormat('en', { numeric: 'auto' });
  for (const [unit, size] of units) {
    if (Math.abs(seconds) >= size || unit === 'second') {
      return format.format(Math.round(seconds / size), unit);
    }
  }
  return '';
}

/**
 * Milliseconds since 1970 of a date written as ISO 8601 or
 * "YYYY-MM-DD HH:MM[:SS[.mmm]]"; without an offset it is read in
 * `timeZone`. Null if it cannot be read.
 */
export function parseDateText(text: string, timeZone: string): number | null {
  const trimmed = text.trim();
  const local = /^(\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2})(?::(\d{2})(?:\.(\d{1,3}))?)?)?$/.exec(trimmed);
  if (local) {
    const [, y, mo, d, h, mi, s, frac] = local;
    const fields = [Number(y), Number(mo) - 1, Number(d), Number(h ?? 0), Number(mi ?? 0), Number(s ?? 0),
      Number((frac ?? '0').padEnd(3, '0'))] as const;
    const guess = Date.UTC(...fields);
    if (Number.isNaN(guess)) return null;
    // The zone's offset at the guess, then again at the result (offsets
    // change around daylight saving time).
    let result = guess - zoneOffsetMinutes(guess, timeZone) * 60000;
    result = guess - zoneOffsetMinutes(result, timeZone) * 60000;
    return result;
  }
  // With an offset or "Z": the browser reads it.
  if (!/^\d{4}-\d{2}-\d{2}T.*(Z|[+-]\d{2}:?\d{2})$/i.test(trimmed)) return null;
  const parsed = Date.parse(trimmed);
  return Number.isNaN(parsed) ? null : parsed;
}

/** The IANA time zones this browser knows. */
export function timeZones(): string[] {
  const supported = (Intl as unknown as { supportedValuesOf?: (key: string) => string[] }).supportedValuesOf;
  const zones = supported ? supported('timeZone') : [];
  return zones.includes('UTC') ? zones : ['UTC', ...zones];
}
