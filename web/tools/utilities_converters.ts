/**
 * The text converters of the utilities (027): Base64, URL encoding, HTML
 * entities, JSON, text and hex, hashes. Each converts as the input changes.
 */
import { button, element } from '../shared/dom.js';
import { decodeBase64, encodeBase64, fileBase64 } from './util_base64.js';
import { failed, ok, type Converted } from './util_common.js';
import { hash, kHashAlgorithms, type HashAlgorithm } from './util_hash.js';
import { decodeHtml, encodeHtml } from './util_html.js';
import { escapeJson, reformatJson, unescapeJson } from './util_json.js';
import { formatInBase, hexToText, parseInBase, textToHex, type Base } from './util_numbers.js';
import { decodeUrl, encodeUrl, type UrlMode } from './util_url.js';
import {
  checkbox, debounce, fileDrop, loadOptions, Output, row, saveOptions, segmented, select, textField,
} from './utilities_ui.js';

/** One utility in the list. */
export interface Utility {
  readonly id: string;
  readonly title: string;
  /** Words it is also found by when filtering the list. */
  readonly keywords: string;
  readonly element: HTMLElement;
  /** Text sent from the page ("Decode with…"): decoded, where the utility decodes. */
  setInput(text: string): void;
}

type Direction = 'encode' | 'decode';

interface TwoWaySpec<O extends { mode: Direction }> {
  readonly id: string;
  readonly title: string;
  readonly keywords: string;
  readonly labels: readonly [string, string];
  readonly defaults: O;
  /** Option controls; `changed` saves the options and converts again. */
  controls(options: O, changed: () => void): HTMLElement[];
  convert(text: string, options: O): Converted | Promise<Converted>;
  /** Extra parts under the output (e.g. a file drop). */
  extra?(): HTMLElement[];
}

/** A utility that converts one way or the other, with Swap. */
function twoWay<O extends { mode: Direction }>(spec: TwoWaySpec<O>): Utility {
  const options = loadOptions(spec.id, spec.defaults);
  const root = element('div', { className: 'util-panel' });
  const input = textField('Input', { lines: 5 });
  const output = new Output('Output', 5);
  let version = 0;
  const convert = (): void => {
    const current = ++version;
    void Promise.resolve(spec.convert(input.input.value, options)).then((result) => {
      if (current === version) output.set(result);
    });
  };
  const render = (): void => {
    const direction = segmented('Direction', [['encode', spec.labels[0]], ['decode', spec.labels[1]]], options.mode, (mode) => {
      options.mode = mode;
      changed();
    });
    const swap = button('Swap', () => {
      if (!output.value) return;
      input.input.value = output.value;
      options.mode = options.mode === 'encode' ? 'decode' : 'encode';
      changed();
    }, 'small');
    swap.title = 'Use the output as input, the other way round';
    controlsRow.replaceChildren(direction, ...spec.controls(options, changed), swap);
  };
  const changed = (): void => {
    saveOptions(spec.id, options);
    render();
    convert();
  };
  const controlsRow = row();
  render();
  input.input.addEventListener('input', convert);
  root.append(controlsRow, input.box, output.box, ...(spec.extra?.() ?? []));
  return {
    id: spec.id,
    title: spec.title,
    keywords: spec.keywords,
    element: root,
    setInput(text: string) {
      input.input.value = text;
      options.mode = 'decode';
      changed();
    },
  };
}

export function base64Utility(): Utility {
  const fileOutput = new Output('File as Base64', 3);
  const dataUrl = new Output('File as data URL', 2);
  const fileBox = element('div', { className: 'util-file' });
  fileBox.hidden = true;
  fileBox.append(fileOutput.box, dataUrl.box);
  return twoWay({
    id: 'base64',
    title: 'Base64',
    keywords: 'base64 atob btoa data url',
    labels: ['Encode', 'Decode'],
    defaults: { mode: 'encode' as Direction, urlSafe: false },
    controls: (o, changed) => o.mode === 'encode'
      ? [checkbox('URL-safe (- _ without =)', o.urlSafe, (v) => {
        o.urlSafe = v;
        changed();
      })]
      : [],
    convert: (text, o) => (o.mode === 'encode' ? ok(encodeBase64(text, o.urlSafe)) : decodeBase64(text)),
    extra: () => [
      fileDrop('Or drop a file here', (file, bytes) => {
        const { base64, dataUrl: url } = fileBase64(bytes, file.type);
        fileOutput.set(ok(base64, `${file.name}: ${bytes.length} bytes`));
        dataUrl.set(ok(url));
        fileBox.hidden = false;
      }),
      fileBox,
    ],
  });
}

export function urlEncodingUtility(): Utility {
  return twoWay({
    id: 'url-encoding',
    title: 'URL encoding',
    keywords: 'url percent encode decode %20 uri component query form',
    labels: ['Encode', 'Decode'],
    defaults: { mode: 'encode' as Direction, as: 'component' as UrlMode },
    controls: (o, changed) => [select<UrlMode>('As', [
      ['component', 'URL part (all special characters)'],
      ['url', 'Whole URL (keeps : / ? # & =)'],
      ['form', 'Form data (space as +)'],
    ], o.as, (v) => {
      o.as = v;
      changed();
    })],
    convert: (text, o) => (o.mode === 'encode' ? encodeUrl(text, o.as) : decodeUrl(text, o.as)),
  });
}

export function htmlUtility(): Utility {
  return twoWay({
    id: 'html',
    title: 'HTML entities',
    keywords: 'html entities escape unescape &amp; &lt;',
    labels: ['Encode', 'Decode'],
    defaults: { mode: 'encode' as Direction, allNonAscii: false },
    controls: (o, changed) => o.mode === 'encode'
      ? [checkbox('Also all non-ASCII characters (&#…;)', o.allNonAscii, (v) => {
        o.allNonAscii = v;
        changed();
      })]
      : [],
    convert: (text, o) => ok(o.mode === 'encode' ? encodeHtml(text, o.allNonAscii) : decodeHtml(text)),
  });
}

export function jsonUtility(): Utility {
  return twoWay({
    id: 'json',
    title: 'JSON and escaping',
    keywords: 'json escape string literal quote format pretty minify',
    labels: ['Escape / format', 'Unescape'],
    defaults: { mode: 'encode' as Direction, what: 'escape' as 'escape' | 'format' | 'minify' },
    controls: (o, changed) => o.mode === 'encode'
      ? [select('Do', [['escape', 'Text → JSON string'], ['format', 'Format JSON'], ['minify', 'Minify JSON']] as const,
        o.what, (v) => {
          o.what = v;
          changed();
        })]
      : [],
    convert: (text, o) => {
      if (o.mode === 'decode') return unescapeJson(text);
      if (o.what === 'escape') return ok(escapeJson(text));
      return text.trim() === '' ? ok('') : reformatJson(text, o.what === 'format');
    },
  });
}

export function hexTextUtility(): Utility {
  return twoWay({
    id: 'text-hex',
    title: 'Text and hex bytes',
    keywords: 'hex bytes utf-8 text',
    labels: ['Text → hex', 'Hex → text'],
    defaults: { mode: 'encode' as Direction },
    controls: () => [],
    convert: (text, o) => (o.mode === 'encode' ? ok(textToHex(text)) : hexToText(text)),
  });
}

export function numberBasesUtility(): Utility {
  const root = element('div', { className: 'util-panel' });
  const bases: readonly (readonly [Base, string])[] = [[10, 'Decimal'], [16, 'Hex'], [8, 'Octal'], [2, 'Binary']];
  const fields = new Map<Base, HTMLInputElement | HTMLTextAreaElement>();
  const problem = element('p', { className: 'util-problem' });
  problem.setAttribute('role', 'alert');
  problem.hidden = true;
  for (const [base, label] of bases) {
    const field = textField(label, { lines: 1 });
    fields.set(base, field.input);
    field.input.addEventListener('input', () => {
      const text = field.input.value;
      const value = text.trim() === '' ? null : parseInBase(text, base);
      problem.hidden = value !== null || text.trim() === '';
      problem.textContent = `Not a whole number in ${label.toLowerCase()}.`;
      for (const [other, input] of fields) {
        if (other !== base) input.value = value === null ? '' : formatInBase(value, other);
      }
    });
    root.append(field.box);
  }
  root.append(problem);
  return {
    id: 'number-bases',
    title: 'Number bases',
    keywords: 'number decimal hex octal binary base',
    element: root,
    setInput(text: string) {
      const input = fields.get(10);
      if (input === undefined) return;
      input.value = text;
      input.dispatchEvent(new Event('input'));
    },
  };
}

export function hashUtility(): Utility {
  const options = loadOptions('hash', { algorithm: 'SHA-256' as HashAlgorithm, hmac: false });
  const root = element('div', { className: 'util-panel' });
  const input = textField('Text', { lines: 4 });
  const key = textField('HMAC key', { lines: 1 });
  const hexOut = new Output('Hex', 2);
  const base64Out = new Output('Base64', 1);
  let file: { name: string; bytes: Uint8Array<ArrayBuffer> } | null = null;
  const fileNote = element('p', { className: 'util-note' });
  let version = 0;
  const run = (): void => {
    const current = ++version;
    const data = file?.bytes ?? new TextEncoder().encode(input.input.value);
    hash(data, options.algorithm, options.hmac ? key.input.value : null).then((digest) => {
      if (current !== version) return;
      hexOut.set(ok(digest.hex));
      base64Out.set(ok(digest.base64));
    }, (error: unknown) => {
      if (current === version) hexOut.set(failed(String(error)));
    });
  };
  const controls = row();
  const render = (): void => {
    controls.replaceChildren(
      select<HashAlgorithm>('Algorithm', kHashAlgorithms.map((a) => [a, a] as const), options.algorithm, (v) => {
        options.algorithm = v;
        changed();
      }),
      checkbox('HMAC with a key', options.hmac, (v) => {
        options.hmac = v;
        changed();
      }),
    );
    key.box.hidden = !options.hmac;
  };
  const changed = (): void => {
    saveOptions('hash', options);
    render();
    run();
  };
  render();
  input.input.addEventListener('input', () => {
    file = null;
    fileNote.hidden = true;
    run();
  });
  key.input.addEventListener('input', debounce(run));
  const drop = fileDrop('Or drop a file here', (f, bytes) => {
    file = { name: f.name, bytes };
    fileNote.textContent = `Hash of the file ${f.name} (${bytes.length} bytes); typing text hashes the text again.`;
    fileNote.hidden = false;
    run();
  });
  fileNote.hidden = true;
  root.append(controls, input.box, drop, fileNote, key.box, hexOut.box, base64Out.box);
  run();
  return {
    id: 'hash',
    title: 'Hashes and HMAC',
    keywords: 'hash sha sha1 sha256 sha512 hmac digest checksum',
    element: root,
    setInput(text: string) {
      input.input.value = text;
      input.input.dispatchEvent(new Event('input'));
    },
  };
}
