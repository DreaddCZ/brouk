/**
 * Cosmetic filtering in web pages (007): hides elements the filter lists
 * name, applies procedural filters (uBlock Origin's :has-text(), :upward()…)
 * and asks for generic hiding rules for the classes and ids on the page.
 *
 * Injected by the renderer before the page's own scripts, in every frame,
 * wrapped in a function so none of its names reach the page. The renderer
 * gives it one native function, `__broukAdblock`, which it takes and removes
 * at once: ("start", receiver) asks the browser for the page's resources,
 * ("classes", json) for generic selectors. Answers come to `receiver`.
 * Scriptlets are run by the renderer itself, not here.
 */

interface ProceduralStep {
  readonly type: string;
  readonly arg: string;
}
interface ProceduralFilter {
  readonly selector: readonly ProceduralStep[];
  readonly action?: { readonly type: string; readonly arg?: string };
}
interface CosmeticAnswer {
  readonly hide?: readonly string[];
  readonly procedural?: readonly string[];
  readonly exceptions?: readonly string[];
  readonly generichide?: boolean;
  readonly generic?: readonly string[];
}

/** Text matching for :has-text() and friends: /regex/flags or plain text. */
function textMatcher(arg: string): (text: string) => boolean {
  const regex = /^\/(.*)\/([imsu]*)$/s.exec(arg);
  if (regex !== null) {
    try {
      const re = new RegExp(regex[1] ?? '', regex[2]);
      return (text) => re.test(text);
    } catch {
      return () => false;
    }
  }
  return (text) => text.includes(arg);
}

/** Applies one procedural step to a set of elements. */
function step(nodes: Element[], s: ProceduralStep, root: Document): Element[] {
  switch (s.type) {
    case 'css-selector': {
      if (nodes.length === 0) {
        try {
          return Array.from(root.querySelectorAll(s.arg));
        } catch {
          return [];
        }
      }
      const out: Element[] = [];
      for (const node of nodes) {
        try {
          // A relative selector (" > .x", " .y") continues from the node.
          const found = s.arg.trimStart().startsWith('>') || s.arg.startsWith(' ')
            ? node.querySelectorAll(`:scope${s.arg}`)
            : node.matches(s.arg) ? [node] : [];
          out.push(...Array.from(found));
        } catch {
          // Invalid selector: no match.
        }
      }
      return out;
    }
    case 'has-text': {
      const match = textMatcher(s.arg);
      return nodes.filter((n) => match(n.textContent ?? ''));
    }
    case 'min-text-length': {
      const min = Number(s.arg);
      return nodes.filter((n) => (n.textContent ?? '').length >= min);
    }
    case 'matches-attr': {
      const [name = '', value = ''] = s.arg.split('=').map((p) => p.replace(/^"|"$/g, ''));
      const nameMatch = textMatcher(name);
      const valueMatch = textMatcher(value);
      return nodes.filter((n) =>
        Array.from(n.attributes).some((a) => nameMatch(a.name) && (value === '' || valueMatch(a.value))),
      );
    }
    case 'matches-css':
    case 'matches-css-before':
    case 'matches-css-after': {
      const pseudo = s.type === 'matches-css' ? null : s.type === 'matches-css-before' ? '::before' : '::after';
      const colon = s.arg.indexOf(':');
      const property = s.arg.slice(0, colon).trim();
      const match = textMatcher(s.arg.slice(colon + 1).trim());
      return nodes.filter((n) => match(getComputedStyle(n, pseudo).getPropertyValue(property)));
    }
    case 'matches-path': {
      const match = textMatcher(s.arg);
      return match(location.pathname + location.search) ? nodes : [];
    }
    case 'upward': {
      const count = Number(s.arg);
      return nodes
        .map((n) => {
          if (Number.isInteger(count) && count > 0) {
            let at: Element | null = n;
            for (let i = 0; i < count && at !== null; i++) at = at.parentElement;
            return at;
          }
          try {
            return n.parentElement?.closest(s.arg) ?? null;
          } catch {
            return null;
          }
        })
        .filter((n): n is Element => n !== null);
    }
    case 'xpath': {
      const out: Element[] = [];
      for (const context of nodes.length === 0 ? [root.documentElement] : nodes) {
        try {
          const result = root.evaluate(s.arg, context, null, XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null);
          for (let i = 0; i < result.snapshotLength; i++) {
            const item = result.snapshotItem(i);
            if (item instanceof Element) out.push(item);
          }
        } catch {
          // Invalid expression.
        }
      }
      return out;
    }
    default:
      return [];
  }
}

function matchProcedural(filter: ProceduralFilter, root: Document): Element[] {
  let nodes: Element[] = [];
  for (const s of filter.selector) {
    nodes = step(nodes, s, root);
    if (nodes.length === 0) return [];
  }
  return nodes;
}

function applyAction(node: Element, filter: ProceduralFilter): void {
  const action = filter.action;
  if (action === undefined) {
    (node as HTMLElement).style?.setProperty('display', 'none', 'important');
    return;
  }
  switch (action.type) {
    case 'remove':
      node.remove();
      break;
    case 'style':
      (node as HTMLElement).style.cssText += `;${action.arg ?? ''}`;
      break;
    case 'remove-attr':
      node.removeAttribute(action.arg ?? '');
      break;
    case 'remove-class':
      node.classList.remove(action.arg ?? '');
      break;
  }
}

/** The page's style element for hiding rules, added on first use. */
function addHidingStyle(selectors: readonly string[]): void {
  if (selectors.length === 0) return;
  const style = document.createElement('style');
  // In chunks: one invalid selector would drop a whole rule.
  const chunks: string[] = [];
  for (let i = 0; i < selectors.length; i += 100) {
    chunks.push(`${selectors.slice(i, i + 100).join(',\n')} { display: none !important; }`);
  }
  style.textContent = chunks.join('\n');
  (document.head ?? document.documentElement).appendChild(style);
}

function start(native: (kind: string, payload: unknown) => void): void {
  let procedural: ProceduralFilter[] = [];
  let exceptions: readonly string[] = [];
  let generic = true;
  const seenClasses = new Set<string>();
  const seenIds = new Set<string>();
  let pendingClasses: string[] = [];
  let pendingIds: string[] = [];
  let sendTimer: number | undefined;
  let procTimer: number | undefined;

  const collect = (root: ParentNode): void => {
    const elements = root instanceof Element ? [root, ...Array.from(root.querySelectorAll('[class],[id]'))] : Array.from(root.querySelectorAll('[class],[id]'));
    for (const element of elements) {
      if (element.id !== '' && !seenIds.has(element.id)) {
        seenIds.add(element.id);
        pendingIds.push(element.id);
      }
      for (const name of Array.from(element.classList)) {
        if (!seenClasses.has(name)) {
          seenClasses.add(name);
          pendingClasses.push(name);
        }
      }
    }
    if ((pendingClasses.length > 0 || pendingIds.length > 0) && sendTimer === undefined) {
      sendTimer = window.setTimeout(() => {
        sendTimer = undefined;
        native('classes', JSON.stringify({ classes: pendingClasses, ids: pendingIds, exceptions }));
        pendingClasses = [];
        pendingIds = [];
      }, 50);
    }
  };

  const runProcedural = (): void => {
    for (const filter of procedural) {
      for (const node of matchProcedural(filter, document)) applyAction(node, filter);
    }
  };

  const observer = new MutationObserver((mutations) => {
    if (generic) {
      for (const mutation of mutations) {
        for (const node of Array.from(mutation.addedNodes)) {
          if (node instanceof Element) collect(node);
        }
        if (mutation.type === 'attributes' && mutation.target instanceof Element) collect(mutation.target);
      }
    }
    if (procedural.length > 0 && procTimer === undefined) {
      procTimer = window.setTimeout(() => {
        procTimer = undefined;
        runProcedural();
      }, 100);
    }
  });

  const receive = (json: string): void => {
    let answer: CosmeticAnswer;
    try {
      answer = JSON.parse(json) as CosmeticAnswer;
    } catch {
      return;
    }
    if (answer.generic !== undefined) {
      addHidingStyle(answer.generic);
      return;
    }
    addHidingStyle(answer.hide ?? []);
    exceptions = answer.exceptions ?? [];
    generic = answer.generichide !== true;
    procedural = [];
    for (const text of answer.procedural ?? []) {
      try {
        procedural.push(JSON.parse(text) as ProceduralFilter);
      } catch {
        // Skip what cannot be read.
      }
    }
    const begin = (): void => {
      if (generic) collect(document);
      runProcedural();
      observer.observe(
        document.documentElement,
        generic
          ? { childList: true, subtree: true, attributes: true, attributeFilter: ['class', 'id'] }
          : { childList: true, subtree: true },
      );
    };
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', begin, { once: true });
    } else {
      begin();
    }
  };

  native('start', receive);
}

(function () {
  const global = globalThis as unknown as Record<string, unknown>;
  const native = global['__broukAdblock'];
  delete global['__broukAdblock'];
  if (typeof native === 'function') {
    start(native as (kind: string, payload: unknown) => void);
  }
})();
