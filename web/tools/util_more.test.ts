import { test } from 'node:test';
import assert from 'node:assert/strict';
import { describeCron, nextRuns, parseCron, type Cron } from './util_cron.js';
import { colorForms, parseColor } from './util_color.js';
import { invalidFlags, runRegex } from './util_regex.js';

function cron(text: string): Cron {
  const parsed = parseCron(text);
  if (typeof parsed === 'string') throw new Error(parsed);
  return parsed;
}

test('cron in words', () => {
  assert.equal(describeCron(cron('30 8 * * 1-5')), 'At 08:30, on Monday through Friday');
  assert.equal(describeCron(cron('0 */15 * * * *')), 'Every 15 minutes');
  assert.equal(describeCron(cron('*/15 * * * *')), 'Every 15 minutes');
  assert.equal(describeCron(cron('@daily')), 'At 00:00');
  assert.equal(describeCron(cron('0 9-17 * * MON-FRI')), 'At minute 0, between 09:00 and 17:59, on Monday through Friday');
  assert.equal(describeCron(cron('0 * * * *')), 'At minute 0, every hour');
  assert.equal(describeCron(cron('* * * * *')), 'Every minute');
  assert.equal(describeCron(cron('*/10 * * * * *')), 'Every 10 seconds');
  assert.equal(describeCron(cron('0 0 1,15 * *')), 'At 00:00, on day 1 and 15 of the month');
  assert.equal(describeCron(cron('0 12 * JAN,JUL ?')), 'At 12:00, in January and July');
  assert.equal(describeCron(cron('15 10 1 * 1')), 'At 10:15, on day 1 of the month or on Monday');
});

test('cron errors point at the field', () => {
  assert.equal(parseCron('61 * * * *'), 'Minute field: 61 is out of range 0–59.');
  assert.equal(parseCron('* * *'), 'Expected 5 fields (minute hour day month weekday) or 6 with seconds first; found 3.');
  assert.ok(String(parseCron('0 0 L * *')).includes('not supported'));
  assert.ok(String(parseCron('0 0 * * 8')).startsWith('Day of week field'));
});

test('cron next runs', () => {
  // Friday 2024-11-01 09:00 local time.
  const from = new Date(2024, 10, 1, 9, 0, 0);
  const runs = nextRuns(cron('30 8 * * 1-5'), from, 5).map((d) => [d.getMonth() + 1, d.getDate(), d.getHours(), d.getMinutes()]);
  assert.deepEqual(runs, [[11, 4, 8, 30], [11, 5, 8, 30], [11, 6, 8, 30], [11, 7, 8, 30], [11, 8, 8, 30]]);
  const quarter = nextRuns(cron('0 */15 * * * *'), from, 2).map((d) => d.getMinutes());
  assert.deepEqual(quarter, [15, 30]);
  assert.equal(nextRuns(cron('0 0 30 2 *'), from, 5).length, 0);
  assert.equal(nextRuns(cron('0 0 29 2 *'), from, 1)[0]?.getFullYear(), 2028);
});

test('colours in every form', () => {
  const none = (): null => null;
  const blue = colorForms(parseColor('#3366cc', none) ?? { r: 0, g: 0, b: 0, a: 0 });
  assert.deepEqual(blue, { hex: '#3366cc', rgb: 'rgb(51, 102, 204)', hsl: 'hsl(220, 60%, 50%)' });
  for (const text of ['rgb(51, 102, 204)', 'hsl(220, 60%, 50%)', 'rgb(51 102 204)', '#36c']) {
    assert.deepEqual(colorForms(parseColor(text, none) ?? { r: 0, g: 0, b: 0, a: 0 }), blue, text);
  }
  const half = colorForms(parseColor('#3366cc80', none) ?? { r: 0, g: 0, b: 0, a: 0 });
  assert.deepEqual(half, { hex: '#3366cc80', rgb: 'rgba(51, 102, 204, 0.5)', hsl: 'hsla(220, 60%, 50%, 0.5)' });
  assert.deepEqual(colorForms(parseColor('hsla(220, 60%, 50%, 0.5)', none) ?? { r: 0, g: 0, b: 0, a: 0 }).hex, '#3366cc80');
  const named = (name: string): string | null => (name === 'rebeccapurple' ? '#663399' : null);
  assert.equal(parseColor('RebeccaPurple', named)?.r, 0x66);
  assert.equal(parseColor('notacolour', named), null);
  assert.equal(parseColor('rgb(1, 2)', none), null);
});

test('regular expressions', () => {
  const result = runRegex('(?<y>\\d{4})-(\\d{2})', 'g', '2024-10 and 2025-01', '$2/$<y>');
  assert.ok(result.ok);
  if (!result.ok) return;
  assert.equal(result.matches.length, 2);
  assert.deepEqual(result.matches[0]?.groups, [{ name: '1', value: '2024' }, { name: '2', value: '10' }, { name: 'y', value: '2024' }]);
  assert.equal(result.replaced, '10/2024 and 01/2025');
  const once = runRegex('a', '', 'aaa', null);
  assert.equal(once.ok && once.matches.length, 1);
  const empty = runRegex('x*', 'g', 'ab', null);
  assert.equal(empty.ok && empty.matches.length, 3);
  assert.equal(runRegex('(', 'g', 'x', null).ok, false);
  assert.equal(invalidFlags('gx'), 'Unknown flag “x”.');
  assert.equal(invalidFlags('gig'), 'A flag is repeated.');
});
