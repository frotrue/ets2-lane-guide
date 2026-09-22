'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { TrafficSignalReceiver, decodeSnapshot } = require('../src/traffic-signal-receiver.cjs');

const signal = (overrides = {}) => ({ id: 3, x: 100, y: 4, z: 200, cx: 0, cz: 0,
  rotationQuat: [1, 0, 0, 0], type: 1, remaining: 10, rawState: 2, ...overrides });
function snapshot(signals) {
  const buffer = Buffer.alloc(1920);
  signals.forEach((s, index) => {
    const start = index * 48;
    [s.x, s.y, s.z].forEach((value, i) => buffer.writeFloatLE(value, start + i * 4));
    buffer.writeInt16LE(s.cx, start + 12); buffer.writeInt16LE(s.cz, start + 14);
    s.rotationQuat.forEach((value, i) => buffer.writeFloatLE(value, start + 16 + i * 4));
    buffer.writeInt32LE(s.type, start + 32); buffer.writeFloatLE(s.remaining, start + 36);
    buffer.writeInt32LE(s.rawState, start + 40); buffer.writeInt32LE(s.id, start + 44);
  });
  return buffer;
}
const packet = (signals, sequence = 1, overrides = {}) => JSON.stringify({
  protocol: 1, source: 'ets2la-semaphore-reader', sequence, connected: true,
  pluginVersion: 1610, data: snapshot(signals).toString('hex'), ...overrides,
});
const target = (overrides = {}) => ({ id: 3, positions: [{ x: 100, y: 4, z: 200 }], ...overrides });
const select = (receiver, targets = [target()], now = 1200) => receiver.select(targets, { gameVersion: '1.61.1.0', now });
function ticking(signals = [signal()]) {
  const receiver = new TrafficSignalReceiver();
  assert.equal(receiver.ingest(packet(signals), 1000), true);
  assert.equal(receiver.ingest(packet(signals.map(s => ({ ...s, remaining: s.remaining - 0.2 })), 2), 1200), true);
  return receiver;
}

test('decodes the published 48-byte packed record and signed world cells', () => {
  const [decoded] = decodeSnapshot(snapshot([signal({ cx: -2, cz: 3 })]));
  assert.equal(decoded.x, -924); assert.equal(decoded.y, 4); assert.equal(decoded.z, 1736);
  assert.equal(decoded.id, 3); assert.equal(decoded.rawState, 2);
  assert.deepEqual(decoded.rotationQuat, [1, 0, 0, 0]);
  assert.deepEqual(decodeSnapshot(snapshot([signal({ type: 2 })])), []);
  assert.equal(decodeSnapshot(Buffer.alloc(1919)), null);
  for (const overrides of [{ x: NaN }, { remaining: -1 }, { remaining: Infinity },
    { rawState: 17 }, { type: 7 }, { id: -1 }, { rotationQuat: [0, 0, 0, 0] }]) {
    assert.equal(decodeSnapshot(snapshot([signal(overrides)])), null);
  }
});

test('requires observed timer progress before showing a fresh live countdown', () => {
  const receiver = new TrafficSignalReceiver();
  receiver.ingest(packet([signal()]), 1000);
  assert.equal(select(receiver, [target()], 1000), null);
  receiver.ingest(packet([signal({ remaining: 9.8 })], 2), 1200);
  assert.deepEqual(select(receiver), { state: 'red', label: '적색 · 전환까지', remainingSeconds: 10, source: 'live' });
});

test('matches the route ID and a locator in 3D, never just the nearest light', () => {
  const receiver = ticking([signal({ id: 8, x: 99 }), signal(), signal({ x: 500, rawState: 8 })]);
  assert.equal(select(receiver).state, 'red');
  assert.equal(select(receiver, [target({ id: 9 })]), null);
  assert.equal(select(receiver, [target({ positions: [{ x: 100, y: 12, z: 200 }] })]), null);
  assert.equal(select(receiver, [target({ positions: [{ x: 110, y: 4, z: 200 }] })]), null);
  assert.equal(select(receiver, []), null);
  assert.equal(select(receiver, [target({ positions: [{ x: null, y: 4, z: 200 }] })]), null);
});

test('all route signal groups must match; repeated locator IDs may have several positions', () => {
  const receiver = ticking([signal(), signal({ id: 4, x: 120 })]);
  const targets = [target({ positions: [{ x: 90, y: 4, z: 200 }, { x: 100, y: 4, z: 200 }] }),
    target({ id: 4, positions: [{ x: 120, y: 4, z: 200 }] })];
  assert.equal(select(receiver, targets).state, 'red');
  assert.equal(select(receiver, [...targets, target({ id: 5 })]), null);
});

test('contradictory matched runtime lights suppress the entire indication', () => {
  const targets = [target({ positions: [{ x: 100, y: 4, z: 200 }, { x: 105, y: 4, z: 200 }] })];
  for (const overrides of [{ rawState: 8 }, { remaining: 8 }]) {
    assert.equal(select(ticking([signal(), signal({ x: 105, ...overrides })]), targets), null);
  }
  assert.equal(select(ticking([signal(), signal({ rawState: 8 })])), null);
  assert.equal(select(ticking([signal(), signal({ x: 101, remaining: 9.8 }),
    signal({ x: 99, remaining: 10.2 })])), null, 'Compare the full range, not only against the first light');
});

test('identical runtime identities are allowed only when every copy agrees', () => {
  assert.equal(select(ticking([signal(), signal()])).state, 'red');
  assert.equal(select(ticking([signal(), signal({ remaining: 9.9 })])).state, 'red');
  assert.equal(select(ticking([signal(), signal({ remaining: 7 })])), null);
});

test('red-yellow remains a stop indication; off, flashing, and gates are hidden', () => {
  const redYellow = select(ticking([signal({ rawState: 4 })]));
  assert.equal(redYellow.state, 'red'); assert.equal(redYellow.label, '적색·황색 · 전환까지');
  assert.equal(select(ticking([signal({ rawState: 1 })])).state, 'amber');
  assert.equal(select(ticking([signal({ rawState: 8 })])).state, 'green');
  for (const overrides of [{ rawState: 0 }, { rawState: 32 }, { type: 2 }]) {
    assert.equal(select(ticking([signal(overrides)])), null);
  }
});

test('stale snapshots and a frozen producer cannot remain live through fresh reads', () => {
  const receiver = ticking();
  assert.equal(select(receiver, [target()], 1951), null);
  for (let i = 1; i <= 6; i++) receiver.ingest(packet([signal({ remaining: 9.8 })], i + 2), 1200 + i * 200);
  assert.equal(select(receiver, [target()], 2400), null);
  receiver.ingest(packet([signal({ remaining: 9.6 })], 9), 2600);
  assert.equal(select(receiver, [target()], 2600).source, 'live');
  receiver.clear();
  assert.equal(select(receiver, [target()], 2600), null);
});

test('unsupported producer/map versions and malformed input clear previously valid data', () => {
  assert.equal(ticking().select([target()], { gameVersion: '1.62.0.0', now: 1200 }), null);
  for (const bad of [packet([signal()], 3, { pluginVersion: 1600 }), '{', 'null',
    packet([signal()], 3, { data: 'ff' }), packet([signal()], 3, { data: 'z'.repeat(3840) })]) {
    const receiver = ticking();
    assert.equal(receiver.ingest(bad, 1400), false);
    assert.equal(select(receiver, [target()], 1400), null);
  }
  const receiver = ticking();
  assert.equal(receiver.ingest(packet([], 3, { connected: false }), 1400), true);
  assert.equal(select(receiver, [target()], 1400), null);
});

test('resets liveness on timer restart, a backwards clock, or non-sequential samples', () => {
  for (const [remaining, sequence, now] of [[15, 3, 1400], [9.6, 1, 1400], [9.6, 3, 1100]]) {
    const receiver = ticking();
    receiver.ingest(packet([signal({ remaining })], sequence), now);
    assert.equal(select(receiver, [target()], now), null);
  }
});
