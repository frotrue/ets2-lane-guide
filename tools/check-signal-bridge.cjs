'use strict';
const { spawn } = require('node:child_process');
const path = require('node:path');
const readline = require('node:readline');
const assert = require('node:assert/strict');
const { TrafficSignalReceiver } = require('../src/traffic-signal-receiver.cjs');
const root = path.join(__dirname, '..');

function fixture(remaining) {
  const bytes = Buffer.alloc(1920);
  bytes.writeFloatLE(100, 0); bytes.writeFloatLE(4, 4); bytes.writeFloatLE(200, 8);
  bytes.writeFloatLE(1, 16); bytes.writeInt32LE(1, 32);
  bytes.writeFloatLE(remaining, 36); bytes.writeInt32LE(2, 40); bytes.writeInt32LE(3, 44);
  return bytes.toString('base64') + '\n';
}
async function until(predicate, label, ms = 5000) {
  const deadline = Date.now() + ms;
  while (!predicate()) {
    if (Date.now() >= deadline) throw Error(`TIMEOUT: ${label}`);
    await new Promise(resolve => setTimeout(resolve, 25));
  }
}
async function main() {
  assert.equal(process.platform, 'win32', 'Win32 shared-memory test only');
  const receiver = new TrafficSignalReceiver();
  let ready = false, written = 0, sawDisconnected = false, producerExited = false, readerExited = false, error = '';
  const producer = spawn('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', path.join(__dirname, 'signal-test-producer.ps1')],
    { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
  producer.stderr.on('data', data => { error += data.toString(); });
  producer.on('error', e => { error += e.message; });
  producer.on('exit', () => { producerExited = true; });
  const producerLines = readline.createInterface({ input: producer.stdout });
  producerLines.on('line', line => { if (line === 'ready') ready = true; if (line === 'written') written++; });
  let reader, readerLines;
  try {
    await until(() => ready || producerExited || error, 'fixture startup');
    assert.equal(error, '', error); assert.equal(ready, true, 'Fixture did not start');
    producer.stdin.write(fixture(10));
    await until(() => written === 1 || error, 'first fixture write'); assert.equal(error, '', error);
    reader = spawn(path.join(root, 'native/traffic_signal_reader.exe'), [], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    reader.on('exit', () => { readerExited = true; });
    reader.on('error', e => { error += e.message; });
    readerLines = readline.createInterface({ input: reader.stdout });
    readerLines.on('line', line => {
      const packet = JSON.parse(line);
      if (!packet.connected) sawDisconnected = true;
      receiver.ingest(line);
    });
    const targets = [{ id: 3, positions: [{ x: 100, y: 4, z: 200 }] }];
    const selected = () => receiver.select(targets, { gameVersion: '1.61.1.0' });
    await until(() => receiver.signals.length === 1 || error, 'native mapping read'); assert.equal(error, '', error);
    assert.equal(selected(), null, 'A static first sample is unverified');
    await new Promise(resolve => setTimeout(resolve, 250));
    producer.stdin.write(fixture(9.7));
    await until(() => selected()?.source === 'live' || error, 'live ticking sample'); assert.equal(error, '', error);
    assert.equal(selected().state, 'red'); assert.equal(selected().remainingSeconds, 10);
    await until(() => selected() === null, 'frozen timer suppression', 2200);
    sawDisconnected = false;
    producer.stdin.end('stop\n');
    await until(() => producerExited, 'fixture shutdown');
    await until(() => sawDisconnected && receiver.signals.length === 0, 'producer disappearance');
    console.log('PASS: native read-only mapping → decoded ticking countdown → frozen-data suppression → disconnected after producer exit.');
  } finally {
    producer.stdin.end(); producer.kill();
    if (reader) reader.kill();
    producerLines.close(); readerLines?.close();
    await until(() => producerExited && (!reader || readerExited), 'fixture process cleanup');
  }
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
