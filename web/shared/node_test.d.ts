// The few Node.js test APIs our unit tests use (tests run with Node's
// built-in test runner, specs/001). Declared here instead of adding @types/node.

declare module 'node:test' {
  export function test(name: string, fn: () => void | Promise<void>): void;
}

declare module 'node:assert/strict' {
  interface Assert {
    equal(actual: unknown, expected: unknown, message?: string): void;
    deepEqual(actual: unknown, expected: unknown, message?: string): void;
    ok(value: unknown, message?: string): void;
    throws(fn: () => unknown, expected?: RegExp | object): void;
    rejects(promise: Promise<unknown>, expected?: RegExp | object): Promise<void>;
  }
  const assert: Assert;
  export default assert;
}
