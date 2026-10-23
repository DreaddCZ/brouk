import { test } from 'node:test';
import assert from 'node:assert/strict';
import { describeRule, needsHeader, needsValue, parseRule, ruleLimitation, ruleProblem, type Rule } from './rules_logic.js';

const rule = (overrides: Partial<Rule>): Rule => ({
  pattern: 'api.example.com', action: 'set-request-header', header: 'X-Env', value: 'staging', enabled: true,
  ...overrides,
});

test('parse', () => {
  assert.equal(parseRule({ pattern: 'a.com', action: 'block' })?.enabled, true);
  assert.equal(parseRule({ pattern: 'a.com', action: 'explode' }), null);
  assert.equal(parseRule({ action: 'block' }), null);
});

test('describe', () => {
  assert.equal(describeRule(rule({})), 'api.example.com: Set request header X-Env: staging');
  assert.equal(describeRule(rule({ action: 'block' })), 'api.example.com: Block');
  assert.equal(describeRule(rule({ action: 'redirect', value: 'http://localhost:3000/' })),
               'api.example.com: Redirect to http://localhost:3000/');
  assert.equal(describeRule(rule({ action: 'remove-response-header', header: 'CSP' })),
               'api.example.com: Remove response header CSP');
});

test('fields and problems', () => {
  assert.equal(needsHeader('block'), false);
  assert.equal(needsValue('remove-request-header'), false);
  assert.equal(ruleProblem(rule({})), null);
  assert.equal(ruleProblem(rule({ pattern: '*.example.com/api/' })), null);
  assert.ok(ruleProblem(rule({ pattern: 'not a pattern' })));
  assert.ok(ruleProblem(rule({ header: 'X Y' })));
  assert.ok(ruleProblem(rule({ action: 'redirect', value: 'javascript:x' })));
  assert.equal(ruleProblem(rule({ action: 'block', header: '' })), null);
});

test('response header rules are marked as not applied', () => {
  assert.ok(ruleLimitation(rule({ action: 'set-response-header' })));
  assert.ok(ruleLimitation(rule({ action: 'remove-response-header' })));
  assert.equal(ruleLimitation(rule({ action: 'set-request-header' })), null);
  assert.equal(ruleLimitation(rule({ action: 'block' })), null);
});
