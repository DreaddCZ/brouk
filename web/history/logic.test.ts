import { test } from 'node:test';
import assert from 'node:assert/strict';
import { dayLabel, groupByDay, hostLabel, lastVisitLabel, pathOf, sortSites, updateSelection, type Site, type Visit } from './logic.js';

function site(host: string, visits: number, lastVisit: number): Site {
  return { profile: 'default', host, visits, pages: 1, lastVisit };
}

test('sites sort by last visit, visits or host', () => {
  const sites = [site('b.com', 5, 100), site('a.com', 9, 50), site('', 1, 200)];
  assert.deepEqual(sortSites(sites, 'lastVisit').map((s) => s.host), ['', 'b.com', 'a.com']);
  assert.deepEqual(sortSites(sites, 'visits').map((s) => s.host), ['a.com', 'b.com', '']);
  assert.deepEqual(sortSites(sites, 'host').map((s) => hostLabel(s.host)), ['a.com', 'b.com', 'Local files']);
});

test('paths of pages within a site', () => {
  assert.equal(pathOf('https://jira.example.com/browse/BR-1?x=1#y'), '/browse/BR-1?x=1#y');
  assert.equal(pathOf('https://example.com'), '/');
  assert.equal(pathOf('file:///Users/me/a.html'), '/Users/me/a.html');
  assert.equal(pathOf('not a url'), 'not a url');
});

const now = new Date(2026, 9, 23, 15, 0, 0);
function at(day: number, hour: number): number {
  return new Date(2026, 9, day, hour, 0, 0).getTime() / 1000;
}

test('days are labelled today, yesterday, then by date', () => {
  assert.equal(dayLabel(at(23, 1), now, 'en-GB'), 'Today');
  assert.equal(dayLabel(at(22, 23), now, 'en-GB'), 'Yesterday');
  assert.equal(dayLabel(at(19, 12), now, 'en-GB'), 'Monday, 19 October 2026');
});

test('visits are grouped by day in order', () => {
  const visit = (id: number, time: number): Visit => ({ profile: 'default', id, url: 'https://a.com/', title: '', host: 'a.com', time, transition: 'link' });
  const groups = groupByDay([visit(1, at(23, 9)), visit(2, at(23, 8)), visit(3, at(22, 20)), visit(4, at(19, 12))], now, 'en-GB');
  assert.deepEqual(groups.map((g) => [g.label, g.visits.map((v) => v.id)]), [
    ['Today', [1, 2]],
    ['Yesterday', [3]],
    ['Monday, 19 October 2026', [4]],
  ]);
});

test('last visit shows the time today, else the date', () => {
  assert.equal(lastVisitLabel(at(23, 9), now, 'en-GB'), '09:00');
  assert.equal(lastVisitLabel(at(19, 9), now, 'en-GB'), '19 Oct');
  assert.equal(lastVisitLabel(new Date(2025, 0, 2).getTime() / 1000, now, 'en-GB'), '2 Jan 2025');
});

test('checkbox selection with shift ranges', () => {
  const order = ['a', 'b', 'c', 'd'];
  let selected = updateSelection(new Set(), order, null, 'b', false);
  assert.deepEqual([...selected], ['b']);
  selected = updateSelection(selected, order, 'b', 'd', true);
  assert.deepEqual([...selected].sort(), ['b', 'c', 'd']);
  selected = updateSelection(selected, order, 'd', 'c', true);
  assert.deepEqual([...selected].sort(), ['b']);
  selected = updateSelection(selected, order, 'c', 'b', false);
  assert.deepEqual([...selected], []);
});
