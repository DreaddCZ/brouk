/**
 * Password strength (008) with zxcvbn-ts, loaded only when the checkup
 * runs. The build copies the library next to this page as flat files
 * (scripts/vendor_modules.mjs): zx-zxcvbn-ts-*.js.
 */
interface Zxcvbn {
  check(password: string): { score: number } | Promise<{ score: number }>;
}

let estimator: Promise<Zxcvbn> | null = null;

async function load(): Promise<Zxcvbn> {
  // Paths in variables: the files exist only in the built page.
  const corePath = './zx-zxcvbn-ts-core.js';
  const commonPath = './zx-zxcvbn-ts-language-common.js';
  const enPath = './zx-zxcvbn-ts-language-en.js';
  const [core, common, en] = (await Promise.all([import(corePath), import(commonPath), import(enPath)])) as [
    { ZxcvbnFactory: new (options: unknown) => Zxcvbn },
    { dictionary: Record<string, unknown>; adjacencyGraphs: unknown },
    { dictionary: Record<string, unknown>; translations: unknown },
  ];
  return new core.ZxcvbnFactory({
    dictionary: { ...common.dictionary, ...en.dictionary },
    graphs: common.adjacencyGraphs,
    translations: en.translations,
  });
}

/** 0 (very guessable) to 4 (very unguessable) for each password. */
export async function strengthScores(passwords: readonly string[]): Promise<number[]> {
  estimator ??= load();
  const zxcvbn = await estimator;
  const scores: number[] = [];
  for (const password of passwords) {
    scores.push((await zxcvbn.check(password)).score);
  }
  return scores;
}
