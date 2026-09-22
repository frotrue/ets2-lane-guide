'use strict';

// Published packed output layout, not offsets into the game's address space.
// https://github.com/ETS2LA/plugin/tree/7094b334f10b68343d0082f1ef4078669235106b
const SLOT_BYTES = 48, SLOT_COUNT = 40, SNAPSHOT_BYTES = SLOT_BYTES * SLOT_COUNT;
const SOURCE = 'ets2la-semaphore-reader';
const SUPPORTED_PLUGIN_VERSION = 1610; // upstream's stoi("1.61.0" without dots)
const PHASES = new Map([
  [1, { state: 'amber', label: '황색 · 전환까지' }],
  [2, { state: 'red', label: '적색 · 전환까지' }],
  [4, { state: 'red', label: '적색·황색 · 전환까지' }],
  [8, { state: 'green', label: '녹색 · 전환까지' }],
]);
const NEXT = new Map([[1, [2]], [2, [4, 8]], [4, [8]], [8, [1]]]);

function decodeSnapshot(buffer) {
  if (!Buffer.isBuffer(buffer) || buffer.length !== SNAPSHOT_BYTES) return null;
  const signals = [];
  for (let offset = 0; offset < buffer.length; offset += SLOT_BYTES) {
    const type = buffer.readInt32LE(offset + 32);
    if (type === 0 || type === 2) continue; // empty slot or barrier/gate
    if (type !== 1) return null;
    const x = buffer.readFloatLE(offset) + buffer.readInt16LE(offset + 12) * 512;
    const y = buffer.readFloatLE(offset + 4);
    const z = buffer.readFloatLE(offset + 8) + buffer.readInt16LE(offset + 14) * 512;
    const rotationQuat = [16, 20, 24, 28].map(i => buffer.readFloatLE(offset + i));
    const remaining = buffer.readFloatLE(offset + 36);
    const rawState = buffer.readInt32LE(offset + 40);
    const id = buffer.readInt32LE(offset + 44);
    const norm = Math.hypot(...rotationQuat);
    if (![x, y, z, remaining, ...rotationQuat].every(Number.isFinite) ||
        Math.max(Math.abs(x), Math.abs(y), Math.abs(z)) > 1e7 ||
        remaining < 0 || remaining > 3600 || id < 0 ||
        norm < 0.99 || norm > 1.01 || ![0, 1, 2, 4, 8, 32].includes(rawState)) return null;
    signals.push({ id, x, y, z, rotationQuat, remaining, rawState });
  }
  return signals;
}

function signalKey(signal) {
  return `${signal.id}:${signal.x.toFixed(2)}:${signal.y.toFixed(2)}:${signal.z.toFixed(2)}`;
}

class TrafficSignalReceiver {
  constructor() { this.clear(); }
  clear() { this.signals = []; this.previous = new Map(); this.receivedAt = null; this.sequence = -1; }
  ingest(line, now = Date.now()) {
    let packet;
    try {
      if (typeof line !== 'string' || line.length > 4300 || !Number.isFinite(now)) throw Error();
      packet = JSON.parse(line);
      if (!packet || packet.protocol !== 1 || packet.source !== SOURCE ||
          !Number.isSafeInteger(packet.sequence) || packet.sequence < 0 ||
          typeof packet.connected !== 'boolean') throw Error();
      if (!packet.connected) { this.clear(); return true; }
      if (packet.pluginVersion !== SUPPORTED_PLUGIN_VERSION ||
          typeof packet.data !== 'string' || packet.data.length !== SNAPSHOT_BYTES * 2 ||
          !/^[\da-f]+$/i.test(packet.data)) throw Error();
      if (this.receivedAt !== null && now - this.receivedAt < 1500 && packet.sequence <= this.sequence) throw Error();
      const signals = decodeSnapshot(Buffer.from(packet.data, 'hex'));
      if (!signals) throw Error();
      const previous = this.receivedAt !== null && now >= this.receivedAt && now - this.receivedAt <= 750
        ? this.previous : new Map();
      const next = new Map();
      for (const signal of signals) {
        const key = signalKey(signal), old = previous.get(key);
        signal.progressAt = null;
        if (old && now > this.receivedAt) {
          const decrease = old.remaining - signal.remaining;
          const reasonableDecrease = decrease > 0.001 && decrease <= (now - this.receivedAt) / 1000 * 4 + 0.1;
          const phaseChange = old.rawState !== signal.rawState && NEXT.get(old.rawState)?.includes(signal.rawState);
          if ((signal.rawState === old.rawState && reasonableDecrease) || phaseChange) signal.progressAt = now;
          else if (signal.rawState === old.rawState && Math.abs(decrease) <= 0.001) signal.progressAt = old.progressAt;
        }
        // Mirrors can repeat an identity. Selection below checks every matched
        // runtime copy, so inconsistent copies cannot produce a displayed value.
        next.set(key, signal);
      }
      this.signals = signals; this.previous = next; this.receivedAt = now; this.sequence = packet.sequence;
      return true;
    } catch {
      this.clear(); return false;
    }
  }

  select(targets, { gameVersion, now = Date.now() } = {}) {
    if (!/^1\.61(?:\.|$)/.test(gameVersion || '') || !Number.isFinite(now) ||
        this.receivedAt === null || now < this.receivedAt || now - this.receivedAt > 750 ||
        !Array.isArray(targets) || !targets.length || targets.length > 40) return null;
    const matches = new Set();
    for (const target of targets) {
      if (!Number.isInteger(target?.id) || target.id < 0 ||
          !Array.isArray(target.positions) || !target.positions.length ||
          target.positions.some(p => !p || !['x','y','z'].every(k => Number.isFinite(p[k])))) return null;
      // The route establishes movement direction. Position disambiguates reused
      // IDs at other prefabs; proximity alone is never a routing decision.
      const group = this.signals.filter(signal => signal.id === target.id && target.positions.some(p =>
        Math.hypot(signal.x - p.x, signal.y - p.y, signal.z - p.z) <= 2));
      if (!group.length) return null;
      for (const signal of group) matches.add(signal);
    }
    const selected = [...matches], phase = PHASES.get(selected[0]?.rawState);
    const times = selected.map(signal => signal.remaining);
    if (!phase || Math.max(...times) - Math.min(...times) > 0.25 ||
        selected.some(signal => signal.progressAt === null || now - signal.progressAt > 1000 ||
        signal.rawState !== selected[0].rawState)) return null;
    return { ...phase, remainingSeconds: Math.ceil(Math.min(...times)), source: 'live' };
  }
}

module.exports = { TrafficSignalReceiver, decodeSnapshot };
