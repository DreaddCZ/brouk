/**
 * Which inputs of a form are login fields (008): user name, password, new
 * password, one-time code. Pure functions over plain descriptions of the
 * inputs, so they are unit-tested without a DOM; login.ts reads the page.
 *
 * Like Chrome: the autocomplete attribute first, then hints in the field's
 * name, id, label and placeholder (English and Czech).
 */

export interface FieldInfo {
  readonly type: string;          // The input's type, lower case.
  readonly autocomplete: string;  // Lower case.
  readonly name: string;
  readonly id: string;
  readonly label: string;         // Its <label>, aria-label or title.
  readonly placeholder: string;
  readonly inputMode: string;
  readonly maxLength: number;     // -1 when not set.
  readonly visible: boolean;
}

export type LoginKind = 'username' | 'password' | 'new-password' | 'otp';

const TEXT_TYPES = new Set(['text', 'email', 'tel', '']);

function hints(field: FieldInfo): string {
  return `${field.name} ${field.id} ${field.label} ${field.placeholder}`.toLowerCase();
}

function tokens(field: FieldInfo): string[] {
  return field.autocomplete.split(/\s+/).filter((t) => t !== '');
}

const NEW_PASSWORD = /new|confirm|repeat|retype|again|create|choose|nov[eéá]|znovu|zopakuj|potvr[dz]|ov[eě][rř]/;
const CURRENT_PASSWORD = /current|old|existing|st[aá]vaj[ií]c|sou[cč]asn|p[uů]vodn/;
const USERNAME = /user|login|e-?mail|identifier|loginfmt|account|sign.?in|nick|uživatel|uzivatel|přihlaš|prihlas|jméno|jmeno/;
const OTP = /one.?time|otp|totp|2fa|mfa|two.?factor|verification|verify|security.?code|auth.*code|token|k[oó]d|ov[eě][rř]ovac/;
const NOT_USERNAME = /search|hledat|query|captcha|coupon|promo|newsletter/;

/** Whether a text input is a one-time code field. */
export function isOtpField(field: FieldInfo): boolean {
  if (tokens(field).includes('one-time-code')) {
    return true;
  }
  if (!TEXT_TYPES.has(field.type) && field.type !== 'number') {
    return false;
  }
  const text = hints(field);
  if (OTP.test(text) && !USERNAME.test(text)) {
    return true;
  }
  // A short numeric field named "code".
  return /code/.test(text) && field.inputMode === 'numeric' &&
    field.maxLength >= 4 && field.maxLength <= 8;
}

function passwordKind(field: FieldInfo, index: number, count: number): LoginKind {
  const auto = tokens(field);
  if (auto.includes('new-password')) {
    return 'new-password';
  }
  if (auto.includes('current-password')) {
    return 'password';
  }
  const text = hints(field);
  if (CURRENT_PASSWORD.test(text)) {
    return 'password';
  }
  if (NEW_PASSWORD.test(text)) {
    return 'new-password';
  }
  // Two password fields: password and confirmation (sign-up). Three: the
  // old one, the new one and its confirmation (change).
  if (count === 2) {
    return 'new-password';
  }
  if (count >= 3) {
    return index === 0 ? 'password' : 'new-password';
  }
  return 'password';
}

function looksLikeUsername(field: FieldInfo, inLoginForm: boolean): boolean {
  if (!field.visible || !TEXT_TYPES.has(field.type)) {
    return false;
  }
  const auto = tokens(field);
  if (auto.includes('username') || (inLoginForm && auto.includes('email'))) {
    return true;
  }
  const text = hints(field);
  return !NOT_USERNAME.test(text) && (inLoginForm || USERNAME.test(text));
}

/**
 * The login kind of each field of one form (null for other fields). In a
 * form with a password field, the user name is the field marked as one or
 * the last text field before the first password field. A form without
 * password fields counts as the first step of a multi-step login only if
 * a field is clearly a user name (autocomplete or a user name hint).
 */
export function classifyForm(fields: readonly FieldInfo[]): (LoginKind | null)[] {
  const kinds: (LoginKind | null)[] = fields.map(() => null);
  const passwords = fields
    .map((f, i) => ({ f, i }))
    .filter(({ f }) => f.type === 'password' && f.visible);
  passwords.forEach(({ f, i }, n) => {
    kinds[i] = passwordKind(f, n, passwords.length);
  });
  fields.forEach((f, i) => {
    if (kinds[i] === null && f.visible && isOtpField(f)) {
      kinds[i] = 'otp';
    }
  });
  const firstPassword = passwords[0]?.i;
  if (firstPassword !== undefined) {
    let username = fields.findIndex((f, i) =>
      kinds[i] === null && f.visible && tokens(f).includes('username'));
    if (username < 0) {
      for (let i = firstPassword - 1; i >= 0; i--) {
        const f = fields[i];
        if (f !== undefined && kinds[i] === null && looksLikeUsername(f, true)) {
          username = i;
          break;
        }
      }
    }
    if (username >= 0) {
      kinds[username] = 'username';
    }
    return kinds;
  }
  // No password field: a first step ("Enter your email"), if unambiguous.
  const candidates = fields
    .map((f, i) => ({ f, i }))
    .filter(({ f, i }) => kinds[i] === null && looksLikeUsername(f, false));
  if (candidates.length === 1 && candidates[0] !== undefined) {
    const visibleText = fields.filter((f) => f.visible && TEXT_TYPES.has(f.type)).length;
    if (visibleText <= 2) {
      kinds[candidates[0].i] = 'username';
    }
  }
  return kinds;
}
