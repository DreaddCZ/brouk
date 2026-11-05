/**
 * Tab environment (027): the dock's tab sees another time zone, language,
 * location, user agent or colour scheme while its dev tools are on. Each
 * override is switched on and off on its own; the values chosen are
 * remembered, but applied only when switched on.
 */
import { button, element, errorText } from '../shared/dom.js';
import type { ToolsApi } from './api.js';
import {
  isChanged, kLocationPresets, kNoEnvironment, parseLanguages, validLanguages, type EnvironmentState,
  type TabEnvironment,
} from './environment_logic.js';
import { isTimeZone, timeZones } from './util_time.js';
import type { Utility } from './utilities_converters.js';
import { loadOptions, row, saveOptions, uid } from './utilities_ui.js';

interface Choices {
  timeZone: string;
  languages: string;
  /** A preset's label, or "custom". */
  location: string;
  latitude: number;
  longitude: number;
  accuracy: number;
  userAgentPreset: string;
  userAgentCustom: string;
  colorScheme: 'light' | 'dark';
}

function input(value: string, label: string, onChange: (value: string) => void, list?: string): HTMLInputElement {
  const node = element('input');
  node.type = 'text';
  node.value = value;
  node.spellcheck = false;
  node.setAttribute('aria-label', label);
  if (list !== undefined) node.setAttribute('list', list);
  node.addEventListener('change', () => onChange(node.value.trim()));
  return node;
}

function selectOf(label: string, choices: readonly (readonly [string, string])[], value: string,
  onChange: (value: string) => void): HTMLSelectElement {
  const node = element('select');
  node.setAttribute('aria-label', label);
  for (const [key, text] of choices) {
    const option = element('option', { text });
    option.value = key;
    option.selected = key === value;
    node.append(option);
  }
  node.addEventListener('change', () => onChange(node.value));
  return node;
}

export function environmentUtility(api: ToolsApi): Utility {
  const localZone = Intl.DateTimeFormat().resolvedOptions().timeZone;
  const choices = loadOptions<Choices>('environment', {
    timeZone: 'Asia/Tokyo',
    languages: 'de-DE, de',
    location: 'Tokyo',
    latitude: 35.689487,
    longitude: 139.691706,
    accuracy: 150,
    userAgentPreset: 'chrome-android',
    userAgentCustom: '',
    colorScheme: 'dark',
  });
  let state: EnvironmentState = { active: kNoEnvironment, presets: [] };
  let problem: string | null = null;
  const root = element('div', { className: 'util-panel util-environment' });
  const zones = element('datalist');
  zones.id = uid('env-zones');
  for (const zone of timeZones()) {
    const option = element('option');
    option.value = zone;
    zones.append(option);
  }

  const apply = async (next: TabEnvironment): Promise<void> => {
    try {
      await api.setEnvironment(next);
      problem = null;
    } catch (error) {
      problem = errorText(error);
    }
    await load();
  };
  const remember = (): void => saveOptions('environment', choices);

  // What each override is when switched on with the current choices.
  const fromChoices = {
    timeZone: (): string | null => (isTimeZone(choices.timeZone) ? choices.timeZone : null),
    languages: (): string[] | null => {
      const list = parseLanguages(choices.languages);
      return validLanguages(list) ? list : null;
    },
    location: (): TabEnvironment['location'] => {
      if (choices.location === 'unavailable') return 'unavailable';
      const preset = kLocationPresets.find((p) => p.label === choices.location);
      return preset ?? { latitude: choices.latitude, longitude: choices.longitude, accuracy: choices.accuracy, label: '' };
    },
    userAgent: (): TabEnvironment['userAgent'] => (choices.userAgentPreset === 'custom'
      ? (choices.userAgentCustom === '' ? null : { custom: choices.userAgentCustom })
      : { preset: choices.userAgentPreset }),
  };

  /** One override: a switch, its controls, and what to apply. */
  const override = (title: string, on: boolean, controls: HTMLElement[], turnOn: () => TabEnvironment | string,
    turnOff: () => TabEnvironment): HTMLElement => {
    const box = element('div', { className: on ? 'env-row on' : 'env-row' });
    const id = uid('env');
    const toggle = element('input');
    toggle.type = 'checkbox';
    toggle.id = id;
    toggle.checked = on;
    const label = element('label', { text: title });
    label.htmlFor = id;
    toggle.addEventListener('change', () => {
      if (!toggle.checked) {
        void apply(turnOff());
        return;
      }
      const next = turnOn();
      if (typeof next === 'string') {
        problem = next;
        toggle.checked = false;
        render();
      } else {
        void apply(next);
      }
    });
    const controlBox = element('div', { className: 'env-controls' });
    controlBox.append(...controls);
    box.append(toggle, label, controlBox);
    return box;
  };

  /** Changing a value of an override that is on applies it at once. */
  const reapply = (isOn: boolean, next: () => TabEnvironment | string): void => {
    remember();
    if (!isOn) return;
    const value = next();
    if (typeof value === 'string') {
      problem = value;
      render();
    } else {
      void apply(value);
    }
  };

  const render = (): void => {
    const active = state.active;
    const withTimeZone = (): TabEnvironment | string => {
      const zone = fromChoices.timeZone();
      return zone === null ? `“${choices.timeZone}” is not a time zone.` : { ...state.active, timeZone: zone };
    };
    const withLanguages = (): TabEnvironment | string => {
      const list = fromChoices.languages();
      return list === null ? 'Languages are tags such as de-DE, de or en, separated by commas.' : { ...state.active, languages: list };
    };
    const withLocation = (): TabEnvironment | string => {
      const location = fromChoices.location();
      if (location !== 'unavailable' && location !== null &&
        (Math.abs(location.latitude) > 90 || Math.abs(location.longitude) > 180 || !(location.accuracy > 0))) {
        return 'Latitude is −90 to 90, longitude −180 to 180, accuracy above 0 metres.';
      }
      return { ...state.active, location };
    };
    const withUserAgent = (): TabEnvironment | string => {
      const userAgent = fromChoices.userAgent();
      return userAgent === null ? 'Type the user agent text.' : { ...state.active, userAgent };
    };

    const zone = input(choices.timeZone, 'Time zone', (v) => {
      choices.timeZone = v;
      reapply(active.timeZone !== null, withTimeZone);
    }, zones.id);
    const languages = input(choices.languages, 'Languages, in order', (v) => {
      choices.languages = v;
      reapply(active.languages.length > 0, withLanguages);
    });
    languages.placeholder = 'de-DE, de, en';

    const locationChoice = selectOf('Location', [
      ...kLocationPresets.map((p) => [p.label, p.label] as const),
      ['custom', 'Coordinates…'], ['unavailable', 'Location unavailable'],
    ], choices.location, (v) => {
      choices.location = v;
      reapply(active.location !== null, withLocation);
      render();
    });
    const coordinates = element('span', { className: 'env-coordinates' });
    if (choices.location === 'custom') {
      const number = (value: number, label: string, set: (n: number) => void): HTMLInputElement => {
        const node = input(String(value), label, (v) => {
          set(Number(v));
          reapply(active.location !== null, withLocation);
        });
        node.inputMode = 'decimal';
        node.size = 10;
        return node;
      };
      coordinates.append(
        number(choices.latitude, 'Latitude', (n) => { choices.latitude = n; }),
        number(choices.longitude, 'Longitude', (n) => { choices.longitude = n; }),
        number(choices.accuracy, 'Accuracy in metres', (n) => { choices.accuracy = n; }),
      );
    }

    const agentChoice = selectOf('User agent', [...state.presets.map((p) => [p.id, p.label] as const), ['custom', 'Custom…']],
      choices.userAgentPreset, (v) => {
        choices.userAgentPreset = v;
        reapply(active.userAgent !== null, withUserAgent);
        render();
      });
    const agentText = input(choices.userAgentCustom, 'Custom user agent', (v) => {
      choices.userAgentCustom = v;
      reapply(active.userAgent !== null, withUserAgent);
    });
    agentText.hidden = choices.userAgentPreset !== 'custom';
    agentText.classList.add('env-wide');

    const scheme = selectOf('Colour scheme', [['light', 'Light'], ['dark', 'Dark']], choices.colorScheme, (v) => {
      choices.colorScheme = v === 'light' ? 'light' : 'dark';
      reapply(active.colorScheme !== null, () => ({ ...state.active, colorScheme: choices.colorScheme }));
    });

    const status = element('p', { className: isChanged(active) ? 'env-status on' : 'env-status' });
    status.setAttribute('role', 'status');
    status.textContent = isChanged(active)
      ? 'This tab sees the overrides below while its dev tools are on. Pages that already read a value see the change after a reload.'
      : `Nothing is changed: the tab sees this computer's settings (time zone ${localZone}).`;
    const error = element('p', { className: 'util-problem', text: problem ?? '' });
    error.setAttribute('role', 'alert');
    error.hidden = problem === null;

    root.replaceChildren(
      status, error,
      override('Time zone', active.timeZone !== null, [zone, zones], withTimeZone, () => ({ ...state.active, timeZone: null })),
      override('Language', active.languages.length > 0, [languages], withLanguages, () => ({ ...state.active, languages: [] })),
      override('Location', active.location !== null, [locationChoice, coordinates], withLocation,
        () => ({ ...state.active, location: null })),
      override('User agent', active.userAgent !== null, [agentChoice, agentText], withUserAgent,
        () => ({ ...state.active, userAgent: null })),
      override('Colour scheme', active.colorScheme !== null, [scheme],
        () => ({ ...state.active, colorScheme: choices.colorScheme }), () => ({ ...state.active, colorScheme: null })),
      override('Reduced motion', active.reducedMotion, [],
        () => ({ ...state.active, reducedMotion: true }), () => ({ ...state.active, reducedMotion: false })),
      row(
        button('Reload the tab', () => void api.reloadTab().catch((e: unknown) => {
          problem = errorText(e);
          render();
        }), 'small'),
        button('Reset all', () => void apply(kNoEnvironment), 'small'),
      ),
      element('p', { className: 'util-note',
        text: 'A site still needs its location permission; the location only replaces the position it gets. '
          + 'Tabs opened from this tab are not changed. Turning off the dev tools ends all overrides.' }),
    );
  };

  const load = async (): Promise<void> => {
    try {
      state = await api.environment();
    } catch (error) {
      problem = errorText(error);
    }
    render();
  };

  render();
  // Load when shown and when the dock comes back (the bar's Reset may have changed it).
  const observer = new IntersectionObserver((entries) => {
    if (entries.some((e) => e.isIntersecting)) void load();
  });
  observer.observe(root);
  window.addEventListener('focus', () => {
    if (root.isConnected) void load();
  });
  return {
    id: 'environment',
    title: 'Tab environment',
    keywords: 'time zone timezone language locale accept-language location geolocation user agent dark mode emulate',
    element: root,
    setInput() {
      // Nothing to decode.
    },
  };
}
