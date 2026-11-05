/**
 * Cron expressions (027): explained in words, with the next run times.
 * Standard 5 fields (minute hour day month weekday) or 6 with seconds
 * first (Spring, Quartz), and @daily and similar shortcuts.
 */

interface Part {
  readonly from: number;
  readonly to: number;
  readonly step: number;
  /** "*" or "?": the whole range. */
  readonly any: boolean;
}

interface Field {
  readonly parts: readonly Part[];
  readonly values: ReadonlySet<number>;
  /** "*" or "?" alone. */
  readonly all: boolean;
}

interface FieldDef {
  readonly name: string;
  readonly min: number;
  readonly max: number;
  readonly names?: readonly string[];
}

const kMonths = ['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN', 'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC'];
const kDays = ['SUN', 'MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT'];
const kMonthNames = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September',
  'October', 'November', 'December'];
const kDayNames = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];

const kSecond: FieldDef = { name: 'Second', min: 0, max: 59 };
const kMinute: FieldDef = { name: 'Minute', min: 0, max: 59 };
const kHour: FieldDef = { name: 'Hour', min: 0, max: 23 };
const kDay: FieldDef = { name: 'Day of month', min: 1, max: 31 };
const kMonth: FieldDef = { name: 'Month', min: 1, max: 12, names: kMonths };
// 7 is Sunday too.
const kWeekday: FieldDef = { name: 'Day of week', min: 0, max: 7, names: kDays };

const kShortcuts: Readonly<Record<string, string>> = {
  '@yearly': '0 0 1 1 *', '@annually': '0 0 1 1 *', '@monthly': '0 0 1 * *', '@weekly': '0 0 * * 0',
  '@daily': '0 0 * * *', '@midnight': '0 0 * * *', '@hourly': '0 * * * *',
};

export interface Cron {
  readonly second: Field | null;
  readonly minute: Field;
  readonly hour: Field;
  readonly day: Field;
  readonly month: Field;
  readonly weekday: Field;
}

function number(text: string, def: FieldDef): number | null {
  const index = def.names?.indexOf(text.toUpperCase()) ?? -1;
  if (index >= 0) return def === kMonth ? index + 1 : index;
  return /^\d+$/.test(text) ? Number(text) : null;
}

function parseField(text: string, def: FieldDef): Field | string {
  const parts: Part[] = [];
  for (const piece of text.split(',')) {
    const m = /^(\*|\?|[A-Za-z0-9]+)(?:-([A-Za-z0-9]+))?(?:\/(\d+))?$/.exec(piece);
    // Quartz's and Jenkins' extensions: "L", "15W", "5#2", "H".
    const extension = /#|(^|\d)[LWH]$/i.test(piece);
    if (!m || extension) {
      return extension
        ? `${def.name} field: “${piece}” (L, W, # or H) is not supported.`
        : `${def.name} field: “${piece}” is not valid.`;
    }
    const any = m[1] === '*' || m[1] === '?';
    const from = any ? def.min : number(m[1] ?? '', def);
    const to = m[2] !== undefined ? number(m[2], def) : any || m[3] !== undefined ? def.max : from;
    const step = m[3] !== undefined ? Number(m[3]) : 1;
    if (from === null || to === null || (any && m[2] !== undefined)) return `${def.name} field: “${piece}” is not valid.`;
    for (const value of [from, to]) {
      if (value < def.min || value > def.max) {
        return `${def.name} field: ${value} is out of range ${def.min}–${def.max === 7 ? 6 : def.max}.`;
      }
    }
    if (from > to) return `${def.name} field: the range ${from}-${to} goes backwards.`;
    if (step < 1) return `${def.name} field: the step must be at least 1.`;
    parts.push({ from, to, step, any });
  }
  const values = new Set<number>();
  for (const part of parts) {
    for (let v = part.from; v <= part.to; v += part.step) values.add(def === kWeekday && v === 7 ? 0 : v);
  }
  return { parts, values, all: parts.length === 1 && parts[0]?.any === true && parts[0].step === 1 };
}

/** The expression, or what is wrong with it. */
export function parseCron(text: string): Cron | string {
  const trimmed = text.trim();
  if (trimmed.toLowerCase() === '@reboot') return '@reboot runs when the system starts; it has no schedule.';
  const expanded = kShortcuts[trimmed.toLowerCase()] ?? trimmed;
  const fields = expanded.split(/\s+/).filter((f) => f !== '');
  if (fields.length !== 5 && fields.length !== 6) {
    return `Expected 5 fields (minute hour day month weekday) or 6 with seconds first; found ${fields.length}.`;
  }
  const defs = fields.length === 6 ? [kSecond, kMinute, kHour, kDay, kMonth, kWeekday] : [kMinute, kHour, kDay, kMonth, kWeekday];
  const parsed: Field[] = [];
  for (let i = 0; i < fields.length; i++) {
    const field = parseField(fields[i] ?? '', defs[i] ?? kMinute);
    if (typeof field === 'string') return field;
    parsed.push(field);
  }
  const at = (i: number): Field => parsed[i] as Field;
  const offset = fields.length === 6 ? 1 : 0;
  return {
    second: offset === 1 ? at(0) : null,
    minute: at(offset),
    hour: at(offset + 1),
    day: at(offset + 2),
    month: at(offset + 3),
    weekday: at(offset + 4),
  };
}

function dayMatches(cron: Cron, date: Date): boolean {
  const day = cron.day.values.has(date.getDate());
  const weekday = cron.weekday.values.has(date.getDay());
  // As in cron: when both are restricted, either one is enough.
  if (!cron.day.all && !cron.weekday.all) return day || weekday;
  return day && weekday;
}

/** The next `count` run times after `from` (local time), within 5 years. */
export function nextRuns(cron: Cron, from: Date, count: number): Date[] {
  const runs: Date[] = [];
  const d = new Date(from.getTime());
  d.setMilliseconds(0);
  if (cron.second === null) d.setSeconds(0);
  d.setTime(d.getTime() + (cron.second === null ? 60000 : 1000));
  const limit = from.getFullYear() + 5;
  while (runs.length < count && d.getFullYear() <= limit) {
    if (!cron.month.values.has(d.getMonth() + 1)) {
      d.setMonth(d.getMonth() + 1, 1);
      d.setHours(0, 0, 0);
    } else if (!dayMatches(cron, d)) {
      d.setDate(d.getDate() + 1);
      d.setHours(0, 0, 0);
    } else if (!cron.hour.values.has(d.getHours())) {
      d.setHours(d.getHours() + 1, 0, 0);
    } else if (!cron.minute.values.has(d.getMinutes())) {
      d.setMinutes(d.getMinutes() + 1, 0);
    } else if (cron.second !== null && !cron.second.values.has(d.getSeconds())) {
      d.setSeconds(d.getSeconds() + 1);
    } else {
      runs.push(new Date(d.getTime()));
      d.setTime(d.getTime() + (cron.second === null ? 60000 : 1000));
    }
  }
  return runs;
}

function joinWords(items: readonly string[]): string {
  if (items.length <= 1) return items.join('');
  return `${items.slice(0, -1).join(', ')} and ${items[items.length - 1] ?? ''}`;
}

function ordinal(n: number): string {
  const suffix = n % 10 === 1 && n % 100 !== 11 ? 'st' : n % 10 === 2 && n % 100 !== 12 ? 'nd'
    : n % 10 === 3 && n % 100 !== 13 ? 'rd' : 'th';
  return `${n}${suffix}`;
}

/** "5", "1 through 5", "every 2nd from 1 through 9", for each part. */
function describeParts(field: Field, name: (v: number) => string): string {
  return joinWords(field.parts.map((p) => {
    const range = `${name(p.from)} through ${name(p.to)}`;
    if (p.step > 1) return p.any ? `every ${ordinal(p.step)}` : `every ${ordinal(p.step)} from ${range}`;
    return p.from === p.to ? name(p.from) : range;
  }));
}

function single(field: Field | null): number | null {
  const part = field?.parts[0];
  return field !== null && field.parts.length === 1 && part !== undefined && part.from === part.to ? part.from : null;
}

const two = (n: number): string => String(n).padStart(2, '0');

/** "Every N <unit>s" when the field is "*\/N" or "*"; otherwise null. */
function every(field: Field, unit: string): string | null {
  const part = field.parts[0];
  if (field.parts.length !== 1 || part === undefined || !part.any) return null;
  return part.step === 1 ? `every ${unit}` : `every ${part.step} ${unit}s`;
}

/** The expression in words: "At 08:30, on Monday through Friday". */
export function describeCron(cron: Cron): string {
  const clauses: string[] = [];
  const second = cron.second !== null && single(cron.second) === 0 ? null : cron.second;
  const minute = single(cron.minute);
  const hour = single(cron.hour);
  if (minute !== null && hour !== null && (second === null || single(second) !== null)) {
    const sec = second !== null ? `:${two(single(second) ?? 0)}` : '';
    clauses.push(`at ${two(hour)}:${two(minute)}${sec}`);
  } else {
    if (second !== null) clauses.push(every(second, 'second') ?? `at second ${describeParts(second, String)}`);
    if (second === null || !second.all) {
      const everyMinute = every(cron.minute, 'minute');
      if (everyMinute !== null && !(second !== null && cron.minute.all)) clauses.push(everyMinute);
      else if (everyMinute === null) clauses.push(`at minute ${describeParts(cron.minute, String)}`);
    }
    if (!cron.hour.all) {
      const part = cron.hour.parts[0];
      if (cron.hour.parts.length === 1 && part !== undefined && part.step === 1 && part.from !== part.to) {
        clauses.push(`between ${two(part.from)}:00 and ${two(part.to)}:59`);
      } else {
        clauses.push(every(cron.hour, 'hour') ?? `past hour ${describeParts(cron.hour, String)}`);
      }
    } else if (minute !== null && second === null) {
      clauses.push('every hour');
    }
  }
  const days: string[] = [];
  if (!cron.day.all) days.push(`on day ${describeParts(cron.day, String)} of the month`);
  if (!cron.weekday.all) {
    days.push(`on ${describeParts(cron.weekday, (v) => kDayNames[v % 7] ?? String(v))}`);
  }
  if (days.length > 0) clauses.push(days.join(' or '));
  if (!cron.month.all) clauses.push(`in ${describeParts(cron.month, (v) => kMonthNames[v - 1] ?? String(v))}`);
  const text = clauses.join(', ');
  return text.charAt(0).toUpperCase() + text.slice(1);
}
