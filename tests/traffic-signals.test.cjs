'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { trafficSignalState } = require('../src/traffic-signals.cjs');

test('live and unspecified modes never synthesize signal timing', () => {
  for (const mode of ['live', undefined, 'unknown', 'DEMO']) {
    for (const now of [1000, 18000, 90000]) {
      assert.equal(trafficSignalState({ mode, demoStartedAt: 1000, now }), null);
    }
  }
  assert.equal(trafficSignalState(), null);
});

test('demo countdown is explicitly marked and transitions without an obsolete zero', () => {
  const sample = elapsed => trafficSignalState({ mode: 'demo', demoStartedAt: 1000, now: 1000 + elapsed });
  const red = sample(0);
  assert.equal(red.state, 'red');
  assert.equal(red.source, 'demo');
  assert.equal(red.label, '시연 신호');
  const transition = red.remainingSeconds * 1000;
  assert.equal(sample(transition - 1).remainingSeconds, 1);
  const green = sample(transition);
  assert.equal(green.state, 'green');
  const amberAt = transition + green.remainingSeconds * 1000;
  const amber = sample(amberAt);
  assert.equal(amber.state, 'amber');
  assert.deepEqual(sample(amberAt + amber.remainingSeconds * 1000), red);
  for (const elapsed of [0, 1001, transition - 1, transition, amberAt]) {
    const signal = sample(elapsed);
    assert.equal(signal.source, 'demo');
    assert.ok(Number.isInteger(signal.remainingSeconds) && signal.remainingSeconds > 0);
  }
});

test('switching out of demo suppresses even a previously visible countdown', () => {
  const context = { mode: 'demo', demoStartedAt: 1000, now: 6000 };
  assert.equal(trafficSignalState(context).source, 'demo');
  assert.equal(trafficSignalState({ ...context, mode: 'live' }), null);
  // A packet or stale renderer sample cannot opt live mode into signal support.
  assert.equal(trafficSignalState({ ...context, mode: 'live', trafficSignal: {
    state: 'green', remainingSeconds: 9, source: 'live',
  } }), null);
});

test('invalid or backwards demo clocks yield unavailable instead of bogus seconds', () => {
  for (const [demoStartedAt, now] of [
    [undefined, 1000], [1000, 999], [NaN, 1000], [1000, Infinity],
    [1000, NaN], ['1000', 2000], [-Number.MAX_VALUE, Number.MAX_VALUE],
  ]) assert.equal(trafficSignalState({ mode: 'demo', demoStartedAt, now }), null);
});
