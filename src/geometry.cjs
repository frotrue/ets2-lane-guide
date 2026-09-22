'use strict';
const distance = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);
function length(points) { let n = 0; for (let i = 1; i < points.length; i++) n += distance(points[i - 1], points[i]); return n; }
function project(point, points) {
  let best = null, accumulated = 0;
  for (let i = 1; i < points.length; i++) {
    const a = points[i - 1], b = points[i];
    const dx = b[0] - a[0], dy = b[1] - a[1], len = Math.hypot(dx, dy);
    if (len < 0.001) continue;
    const t = Math.max(0, Math.min(1, ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / (len * len)));
    const p = [a[0] + dx * t, a[1] + dy * t, (a[2] || 0) + ((b[2] || 0) - (a[2] || 0)) * t];
    const d = distance(point, p);
    if (!best || d < best.distance) best = { distance: d, along: accumulated + len * t, point: p, tangent: [dx / len, dy / len] };
    accumulated += len;
  }
  return best;
}
function spline(a, b, steps = 16) {
  const d = distance([a.x, a.y], [b.x, b.y]);
  return Array.from({ length: steps + 1 }, (_, i) => {
    const t = i / steps, t2 = t*t, t3 = t2*t;
    const h0 = 2*t3-3*t2+1, h1 = t3-2*t2+t, h2 = -2*t3+3*t2, h3 = t3-t2;
    return [h0*a.x+h1*Math.cos(a.rotation)*d+h2*b.x+h3*Math.cos(b.rotation)*d,
      h0*a.y+h1*Math.sin(a.rotation)*d+h2*b.y+h3*Math.sin(b.rotation)*d,
      (a.z || 0) + ((b.z || 0)-(a.z || 0))*t];
  });
}
function turn(incoming, outgoing) {
  const angle = Math.atan2(incoming[0]*outgoing[1]-incoming[1]*outgoing[0], incoming[0]*outgoing[0]+incoming[1]*outgoing[1]);
  if (Math.abs(angle) > 2.65) return 'uturn';
  if (angle > 0.23) return 'right';
  if (angle < -0.23) return 'left';
  return 'straight';
}
module.exports = { distance, length, project, spline, turn };
