'use strict';
class Telemetry {
  constructor() { this.packet = null; this.receivedAt = 0; }
  ingest(buffer, now = Date.now()) {
    if (buffer.length > 2048) return false;
    let p; try { p = JSON.parse(buffer.toString()); } catch { return false; }
    if (p.protocol !== 1 || p.source !== 'ets2-lane-guide' || typeof p.connected !== 'boolean') return false;
    if (!p.connected) { this.packet = null; this.receivedAt = 0; return true; }
    if (typeof p.paused !== 'boolean' || typeof p.placed !== 'boolean' ||
        !['x','y','z','heading','speed','scale','sequence'].every(k => Number.isFinite(p[k])) ||
        Math.abs(p.x) > 1e7 || Math.abs(p.z) > 1e7 || p.scale <= 0 || p.scale > 1000) return false;
    // Ignore delayed datagrams during a session; a gap allows a restarted plugin.
    if (this.packet && now - this.receivedAt < 1500 && p.sequence <= this.packet.sequence) return false;
    this.packet = p; this.receivedAt = now; return true;
  }
  get(now = Date.now()) { return this.packet && now - this.receivedAt <= 1500 ? this.packet : null; }
}
module.exports = { Telemetry };
