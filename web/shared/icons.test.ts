import { test } from 'node:test';
import assert from 'node:assert/strict';
import { iconOf, parseIcons } from './icons.js';

test('only PNG data URLs are kept as icons', () => {
  const icons = parseIcons({
    icons: {
      'p:1': 'data:image/png;base64,iVBORw0KGgo=',
      'p:2': 'javascript:alert(1)',
      'p:3': 'data:image/png;base64,not base64!',
      'p:4': 5,
    },
  });
  assert.deepEqual([...icons.keys()], ['p:1']);
  assert.deepEqual(iconOf({ icon: 'p:1' }, icons), { icon: 'data:image/png;base64,iVBORw0KGgo=' });
  assert.deepEqual(iconOf({ icon: 'p:2' }, icons), {});
  assert.deepEqual(iconOf({}, icons), {});
  assert.equal(parseIcons({}).size, 0);
  assert.equal(parseIcons(null).size, 0);
});
