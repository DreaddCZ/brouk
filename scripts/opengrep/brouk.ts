// Test cases for brouk.yml, run by scripts/sast (opengrep test). A
// "ruleid" comment marks a line that must match, an "ok" comment one that
// must not.
declare const el: HTMLElement;
declare const a: HTMLAnchorElement;
declare const data: string;
declare const parser: DOMParser;

// ruleid: ts-html-sink
el.innerHTML = data;
// ruleid: ts-html-sink
el.innerHTML = `<b>${data}</b>`;
// ok: ts-html-sink
el.innerHTML = '<b>constant</b>';
// ruleid: ts-html-sink
el.insertAdjacentHTML('beforeend', data);
// ruleid: ts-html-sink
parser.parseFromString(data, 'text/html');
// ok: ts-html-sink
el.textContent = data;

// ruleid: ts-code-from-string
eval(data);
// ruleid: ts-code-from-string
new Function(data);
// ruleid: ts-code-from-string
setTimeout('run()', 10);
// ok: ts-code-from-string
setTimeout(() => {}, 10);

// ruleid: ts-url-sink
a.href = data;
// ruleid: ts-url-sink
window.open(data, '_blank');
// ok: ts-url-sink
a.href = '#top';
// ok: ts-url-sink
a.href = `#${data}`;

// ruleid: ts-cefquery-outside-bridge
Reflect.get(globalThis, 'cefQuery');

// ruleid: ts-postmessage-any-origin
window.postMessage(data, '*');
// ok: ts-postmessage-any-origin
window.postMessage(data, 'brouk://settings');
