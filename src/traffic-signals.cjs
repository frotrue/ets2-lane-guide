'use strict';

// Presentation sample only. SCS SDK 1.14 exposes no live signal phase or
// remaining-time channel. Map intervals and offsets are not a phase clock.
// Do not derive live countdowns from Date.now(), game.time or frame timestamps.
const DEMO_PHASES = [
  { state: 'red', durationMs: 18000 },
  { state: 'green', durationMs: 20000 },
  { state: 'amber', durationMs: 3000 },
];
const DEMO_CYCLE_MS = DEMO_PHASES.reduce((total, phase) => total + phase.durationMs, 0);

/**
 * null means no verified signal timing is available; never infer a green light.
 * A non-null sample is permitted only in explicitly selected demo mode.
 */
function trafficSignalState({ mode, demoStartedAt, now = Date.now() } = {}) {
  if (mode !== 'demo' || !Number.isFinite(demoStartedAt) ||
      !Number.isFinite(now) || now < demoStartedAt) return null;

  let elapsed = (now - demoStartedAt) % DEMO_CYCLE_MS;
  if (!Number.isFinite(elapsed)) return null;
  for (const phase of DEMO_PHASES) {
    if (elapsed < phase.durationMs) return {
      state: phase.state,
      remainingSeconds: Math.ceil((phase.durationMs - elapsed) / 1000),
      source: 'demo',
      label: '시연 신호',
    };
    elapsed -= phase.durationMs;
  }
  return null;
}

module.exports = { trafficSignalState };
