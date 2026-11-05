/**
 * The utilities (027) that are more than a text conversion: Unix time,
 * the URL parser, UUIDs, regular expressions, cron and colours.
 */
import { button, element } from '../shared/dom.js';
import { colorForms, parseColor } from './util_color.js';
import { ok } from './util_common.js';
import { describeCron, nextRuns, parseCron } from './util_cron.js';
import type { RegexResult } from './util_regex.js';
import { invalidFlags } from './util_regex.js';
import {
  formatInZone, isTimeZone, parseDateText, parseTimestamp, relativeTime, timeZones, zoneOffsetText, type TimeUnit,
} from './util_time.js';
import { buildUrl, parseUrl, type UrlParts } from './util_url.js';
import { explainUuid, formatUuid, uuidV4, uuidV7 } from './util_uuid.js';
import type { Utility } from './utilities_converters.js';
import {
  checkbox, copyText, debounce, loadOptions, Output, row, saveOptions, select, textField, uid,
} from './utilities_ui.js';

/** A table of label → value rows, each value with Copy. */
function valueTable(rows: readonly (readonly [string, string])[]): HTMLTableElement {
  const table = element('table', { className: 'util-values' });
  for (const [label, value] of rows) {
    const tr = element('tr');
    const copy = button('Copy', () => void copyText(value), 'small');
    copy.setAttribute('aria-label', `Copy ${label}`);
    const actions = element('td', { className: 'actions' });
    actions.append(copy);
    tr.append(element('th', { text: label }), element('td', { text: value }), actions);
    table.append(tr);
  }
  return table;
}

function problemLine(): HTMLParagraphElement {
  const line = element('p', { className: 'util-problem' });
  line.setAttribute('role', 'alert');
  line.hidden = true;
  return line;
}

function showProblem(line: HTMLParagraphElement, text: string | null): void {
  line.textContent = text ?? '';
  line.hidden = text === null;
}

/** A searchable time zone field (the browser's IANA list). */
function timeZoneField(value: string, onChange: (zone: string) => void): HTMLElement {
  const box = element('label', { className: 'util-option' });
  box.append(element('span', { text: 'Time zone' }));
  const input = element('input');
  input.type = 'text';
  input.value = value;
  input.spellcheck = false;
  const listId = uid('zones');
  input.setAttribute('list', listId);
  const list = element('datalist');
  list.id = listId;
  for (const zone of timeZones()) {
    const option = element('option');
    option.value = zone;
    list.append(option);
  }
  input.addEventListener('change', () => {
    if (isTimeZone(input.value.trim())) onChange(input.value.trim());
    else input.value = value;
  });
  box.append(input, list);
  return box;
}

export function unixTimeUtility(): Utility {
  const localZone = Intl.DateTimeFormat().resolvedOptions().timeZone;
  const options = loadOptions('unix-time', { unit: 'auto' as TimeUnit | 'auto', zone: localZone });
  if (!isTimeZone(options.zone)) options.zone = localZone;
  const root = element('div', { className: 'util-panel' });
  const input = textField('Timestamp or date', {
    lines: 1, placeholder: '1730000000, 1730000000000 or 2024-10-27 03:33:20',
  });
  const result = element('div');
  const problem = problemLine();
  const update = (): void => {
    const text = input.input.value.trim();
    showProblem(problem, null);
    result.replaceChildren();
    if (text === '') return;
    const stamp = parseTimestamp(text, options.unit);
    const ms = stamp !== null ? stamp.ms : parseDateText(text, options.zone);
    if (ms === null) {
      showProblem(problem, 'Not a timestamp or a date (e.g. 2024-10-27 03:33:20, or ISO 8601 with Z or an offset).');
      return;
    }
    const units: Record<TimeUnit, string> = { s: 'seconds', ms: 'milliseconds', us: 'microseconds' };
    if (stamp !== null) result.append(element('p', { className: 'util-note', text: `Read as ${units[stamp.unit]}.` }));
    result.append(valueTable([
      [`${options.zone} (${zoneOffsetText(ms, options.zone)})`, formatInZone(ms, options.zone)],
      ['UTC', formatInZone(ms, 'UTC')],
      ['ISO 8601', new Date(ms).toISOString()],
      ['Relative', relativeTime(ms, Date.now())],
      ['Seconds', String(Math.floor(ms / 1000))],
      ['Milliseconds', String(Math.round(ms))],
    ]));
  };
  const controls = row();
  const render = (): void => {
    controls.replaceChildren(
      select<TimeUnit | 'auto'>('Timestamp in', [['auto', 'Detect'], ['s', 'Seconds'], ['ms', 'Milliseconds'], ['us', 'Microseconds']],
        options.unit, (v) => {
          options.unit = v;
          changed();
        }),
      timeZoneField(options.zone, (zone) => {
        options.zone = zone;
        changed();
      }),
      button('Now', () => {
        input.input.value = String(Date.now());
        update();
      }, 'small'),
    );
  };
  const changed = (): void => {
    saveOptions('unix-time', options);
    render();
    update();
  };
  render();
  input.input.addEventListener('input', update);
  root.append(controls, input.box, problem, result);
  return {
    id: 'unix-time',
    title: 'Unix time',
    keywords: 'unix time timestamp epoch date utc iso 8601 timezone',
    element: root,
    setInput(text: string) {
      input.input.value = text;
      update();
    },
  };
}

export function urlParserUtility(): Utility {
  const root = element('div', { className: 'util-panel' });
  const input = textField('URL', { lines: 2, placeholder: 'https://example.com/path?name=value#part' });
  const problem = problemLine();
  const editor = element('div');
  let parts: UrlParts | null = null;
  const rebuild = (): void => {
    if (parts === null) return;
    const url = buildUrl(parts);
    showProblem(problem, url === null ? 'These parts do not make a valid URL.' : null);
    if (url !== null) input.input.value = url;
  };
  const partField = (label: string, key: Exclude<keyof UrlParts, 'params'>): HTMLElement => {
    const field = textField(label, { lines: 1 });
    field.input.value = parts?.[key] ?? '';
    field.input.addEventListener('change', () => {
      if (parts === null) return;
      parts = { ...parts, [key]: field.input.value };
      rebuild();
    });
    return field.box;
  };
  const render = (): void => {
    editor.replaceChildren();
    if (parts === null) return;
    const grid = element('div', { className: 'util-grid' });
    grid.append(partField('Scheme', 'protocol'), partField('Host', 'hostname'), partField('Port', 'port'),
      partField('User', 'username'), partField('Password', 'password'), partField('Path', 'pathname'),
      partField('Fragment', 'hash'));
    const table = element('table', { className: 'util-params' });
    const head = element('tr');
    head.append(element('th', { text: 'Parameter' }), element('th', { text: 'Value (decoded)' }), element('th'));
    table.append(head);
    parts.params.forEach(([name, value], index) => {
      const tr = element('tr');
      const cell = (text: string, label: string, set: (v: string) => void): HTMLTableCellElement => {
        const td = element('td');
        const field = element('input');
        field.type = 'text';
        field.value = text;
        field.spellcheck = false;
        field.setAttribute('aria-label', label);
        field.addEventListener('change', () => set(field.value));
        td.append(field);
        return td;
      };
      const update = (n: string, v: string): void => {
        if (parts === null) return;
        const params = [...parts.params];
        params[index] = [n, v];
        parts = { ...parts, params };
        rebuild();
      };
      const actions = element('td', { className: 'actions' });
      actions.append(button('Remove', () => {
        if (parts === null) return;
        parts = { ...parts, params: parts.params.filter((_, i) => i !== index) };
        rebuild();
        render();
      }, 'small'));
      tr.append(cell(name, `Name of parameter ${index + 1}`, (n) => update(n, value)),
        cell(value, `Value of ${name}`, (v) => update(name, v)), actions);
      table.append(tr);
    });
    editor.append(grid, element('h2', { text: 'Query parameters' }), table, row(button('Add parameter', () => {
      if (parts === null) return;
      parts = { ...parts, params: [...parts.params, ['name', 'value']] };
      rebuild();
      render();
    }, 'small')));
  };
  const parse = (): void => {
    const text = input.input.value.trim();
    if (text === '') {
      parts = null;
      showProblem(problem, null);
      render();
      return;
    }
    const parsed = parseUrl(text);
    if ('error' in parsed) {
      showProblem(problem, parsed.error);
      return;
    }
    showProblem(problem, null);
    parts = parsed.parts;
    render();
  };
  input.input.addEventListener('input', parse);
  root.append(input.box, problem, editor);
  return {
    id: 'url-parser',
    title: 'URL parser',
    keywords: 'url parse query parameters host path',
    element: root,
    setInput(text: string) {
      input.input.value = text;
      parse();
    },
  };
}

export function uuidUtility(): Utility {
  const options = loadOptions('uuid', { version: 'v4' as 'v4' | 'v7', count: 1, upper: false, hyphens: true });
  const root = element('div', { className: 'util-panel' });
  const output = new Output('Generated', 5);
  const generate = (): void => {
    const list = Array.from({ length: options.count }, () => (options.version === 'v4' ? uuidV4() : uuidV7()));
    output.set(ok(list.map((u) => formatUuid(u, options.upper, options.hyphens)).join('\n')));
  };
  const controls = row();
  const render = (): void => {
    const count = element('label', { className: 'util-option' });
    const number = element('input');
    number.type = 'number';
    number.min = '1';
    number.max = '100';
    number.value = String(options.count);
    number.addEventListener('change', () => {
      options.count = Math.min(100, Math.max(1, Math.round(Number(number.value)) || 1));
      changed();
    });
    count.append(element('span', { text: 'How many' }), number);
    controls.replaceChildren(
      select('Version', [['v4', 'v4 (random)'], ['v7', 'v7 (time-ordered)']] as const, options.version, (v) => {
        options.version = v;
        changed();
      }),
      count,
      checkbox('Upper case', options.upper, (v) => {
        options.upper = v;
        changed();
      }),
      checkbox('Hyphens', options.hyphens, (v) => {
        options.hyphens = v;
        changed();
      }),
      button('Generate', generate, 'small'),
    );
  };
  const changed = (): void => {
    saveOptions('uuid', options);
    render();
    generate();
  };
  render();
  generate();
  const explain = textField('Explain a UUID', { lines: 1, placeholder: '018f2c3e-…' });
  const info = element('div');
  const problem = problemLine();
  const explainNow = (): void => {
    info.replaceChildren();
    const text = explain.input.value.trim();
    if (text === '') {
      showProblem(problem, null);
      return;
    }
    const uuid = explainUuid(text);
    showProblem(problem, uuid === null ? 'Not a UUID (32 hex digits, hyphens optional).' : null);
    if (uuid === null) return;
    const rows: [string, string][] = [['Version', String(uuid.version)], ['Variant', uuid.variant]];
    if (uuid.time !== null) {
      rows.push(['Created (UTC)', formatInZone(uuid.time, 'UTC')], ['Created (relative)', relativeTime(uuid.time, Date.now())]);
    }
    info.append(valueTable(rows));
  };
  explain.input.addEventListener('input', explainNow);
  root.append(controls, output.box, explain.box, problem, info);
  return {
    id: 'uuid',
    title: 'UUID',
    keywords: 'uuid guid generate v4 v7',
    element: root,
    setInput(text: string) {
      explain.input.value = text;
      explainNow();
    },
  };
}

/** Runs regular expressions in a worker, so a runaway pattern can be stopped. */
class RegexRunner {
  #worker: Worker | null = null;
  #pending: ((result: RegexResult) => void) | null = null;
  #timer: ReturnType<typeof setTimeout> | undefined;

  run(pattern: string, flags: string, text: string, replacement: string | null): Promise<RegexResult> {
    this.#cancel();
    const worker = (this.#worker ??= this.#create());
    return new Promise((resolve) => {
      this.#pending = resolve;
      this.#timer = setTimeout(() => {
        this.#worker?.terminate();
        this.#worker = null;
        this.#finish({ ok: false, error: 'The expression took longer than 2 seconds and was stopped (catastrophic backtracking?).' });
      }, 2000);
      worker.postMessage({ pattern, flags, text, replacement });
    });
  }

  #create(): Worker {
    const worker = new Worker('regex_worker.js', { type: 'module' });
    worker.addEventListener('message', (event: MessageEvent<RegexResult>) => this.#finish(event.data));
    return worker;
  }

  #finish(result: RegexResult): void {
    clearTimeout(this.#timer);
    const pending = this.#pending;
    this.#pending = null;
    pending?.(result);
  }

  #cancel(): void {
    if (this.#pending !== null) {
      // A newer run replaces this one: restart the worker if it is busy.
      clearTimeout(this.#timer);
      this.#worker?.terminate();
      this.#worker = null;
      this.#pending = null;
    }
  }
}

export function regexUtility(): Utility {
  const options = loadOptions('regex', { flags: 'g' });
  const root = element('div', { className: 'util-panel' });
  const pattern = textField('Pattern', { lines: 1, placeholder: '(?<year>\\d{4})-(\\d{2})' });
  const flags = textField('Flags', { lines: 1 });
  flags.input.value = options.flags;
  flags.box.classList.add('util-flags');
  const text = textField('Text', { lines: 5 });
  const replacement = textField('Replace with (optional: $1, $<name>)', { lines: 1 });
  const problem = problemLine();
  const highlighted = element('pre', { className: 'util-highlight' });
  const matches = element('div');
  const replaced = new Output('Replaced', 4);
  replaced.box.hidden = true;
  const runner = new RegexRunner();
  root.append(element('p', { className: 'util-note', text: 'JavaScript syntax; other languages differ in details.' }));
  const run = (): void => {
    options.flags = flags.input.value.trim();
    saveOptions('regex', options);
    const flagProblem = invalidFlags(options.flags);
    if (pattern.input.value === '' || flagProblem !== null) {
      showProblem(problem, flagProblem);
      highlighted.replaceChildren();
      matches.replaceChildren();
      replaced.box.hidden = true;
      return;
    }
    const subject = text.input.value;
    const replaceWith = replacement.input.value === '' ? null : replacement.input.value;
    void runner.run(pattern.input.value, options.flags, subject, replaceWith).then((result) => {
      highlighted.replaceChildren();
      matches.replaceChildren();
      if (!result.ok) {
        showProblem(problem, result.error);
        replaced.box.hidden = true;
        return;
      }
      showProblem(problem, null);
      let at = 0;
      for (const m of result.matches) {
        highlighted.append(subject.slice(at, m.index), element('mark', { text: m.text }));
        at = m.index + m.text.length;
      }
      highlighted.append(subject.slice(at));
      const count = result.matches.length;
      matches.append(element('h2', {
        text: `${count === 1 ? '1 match' : `${count} matches`}${result.limited ? ' (only the first ones listed)' : ''}`,
      }));
      if (count > 0) {
        const table = element('table', { className: 'util-matches' });
        const head = element('tr');
        head.append(element('th', { text: '#' }), element('th', { text: 'At' }), element('th', { text: 'Match' }),
          element('th', { text: 'Groups' }));
        table.append(head);
        result.matches.slice(0, 200).forEach((m, i) => {
          const tr = element('tr');
          const groups = m.groups.map((g) => `${g.name}: ${g.value === undefined ? '—' : JSON.stringify(g.value)}`).join('  ');
          tr.append(element('td', { text: String(i + 1) }), element('td', { text: String(m.index) }),
            element('td', { text: JSON.stringify(m.text) }), element('td', { text: groups }));
          table.append(tr);
        });
        matches.append(table);
      }
      replaced.box.hidden = result.replaced === null;
      if (result.replaced !== null) replaced.set(ok(result.replaced));
    });
  };
  const later = debounce(run);
  for (const field of [pattern, flags, text, replacement]) field.input.addEventListener('input', later);
  root.append(row(pattern.box, flags.box), problem, text.box, replacement.box, highlighted, matches, replaced.box);
  return {
    id: 'regex',
    title: 'Regular expressions',
    keywords: 'regex regexp regular expression match replace test',
    element: root,
    setInput(value: string) {
      text.input.value = value;
      run();
    },
  };
}

export function cronUtility(): Utility {
  const root = element('div', { className: 'util-panel' });
  const input = textField('Cron expression', { lines: 1, placeholder: '30 8 * * 1-5' });
  const problem = problemLine();
  const result = element('div');
  const update = (): void => {
    result.replaceChildren();
    const text = input.input.value.trim();
    if (text === '') {
      showProblem(problem, null);
      return;
    }
    const cron = parseCron(text);
    if (typeof cron === 'string') {
      showProblem(problem, cron);
      return;
    }
    showProblem(problem, null);
    const zone = Intl.DateTimeFormat().resolvedOptions().timeZone;
    const runs = nextRuns(cron, new Date(), 5);
    result.append(element('p', { className: 'util-description', text: describeCron(cron) }),
      element('h2', { text: `Next runs (${zone})` }));
    const list = element('ol', { className: 'util-runs' });
    for (const run of runs) list.append(element('li', { text: formatInZone(run.getTime(), zone) }));
    if (runs.length === 0) list.append(element('li', { text: 'Never within the next 5 years.' }));
    result.append(list);
  };
  input.input.addEventListener('input', update);
  root.append(element('p', { className: 'util-note',
    text: '5 fields (minute hour day month weekday) or 6 with seconds first (Spring, Quartz); @daily and similar.' }),
  input.box, problem, result);
  return {
    id: 'cron',
    title: 'Cron expressions',
    keywords: 'cron schedule crontab quartz spring',
    element: root,
    setInput(text: string) {
      input.input.value = text;
      update();
    },
  };
}

/** CSS colour names through the browser (a canvas reads any CSS colour). */
function namedColor(name: string): string | null {
  const context = document.createElement('canvas').getContext('2d');
  if (context === null) return null;
  context.fillStyle = '#010203';
  context.fillStyle = name;
  const value = String(context.fillStyle);
  return value === '#010203' ? null : value;
}

export function colorUtility(): Utility {
  const root = element('div', { className: 'util-panel' });
  const input = textField('Colour', { lines: 1, placeholder: '#3366cc, rgb(51 102 204), hsl(220 60% 50%), teal' });
  const picker = element('input');
  picker.type = 'color';
  picker.setAttribute('aria-label', 'Pick a colour');
  const swatch = element('div', { className: 'util-swatch' });
  const problem = problemLine();
  const forms = element('div');
  const update = (): void => {
    forms.replaceChildren();
    const text = input.input.value.trim();
    if (text === '') {
      showProblem(problem, null);
      swatch.style.background = '';
      return;
    }
    const color = parseColor(text, namedColor);
    showProblem(problem, color === null ? 'Not a colour (hex, rgb(), rgba(), hsl(), hsla() or a CSS name).' : null);
    if (color === null) return;
    const all = colorForms(color);
    swatch.style.background = all.rgb;
    picker.value = all.hex.slice(0, 7);
    forms.append(valueTable([['Hex', all.hex], ['RGB', all.rgb], ['HSL', all.hsl]]));
  };
  input.input.addEventListener('input', update);
  picker.addEventListener('input', () => {
    input.input.value = picker.value;
    update();
  });
  root.append(row(input.box, picker, swatch), problem, forms);
  return {
    id: 'color',
    title: 'Colours',
    keywords: 'colour color hex rgb hsl css picker',
    element: root,
    setInput(text: string) {
      input.input.value = text;
      update();
    },
  };
}

