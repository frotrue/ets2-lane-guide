'use strict';
const { spawn } = require('node:child_process');
const readline = require('node:readline');

function startSignalBridge(executable, receiver) {
  const child = spawn(executable, [], { windowsHide: true, stdio: ['ignore', 'pipe', 'ignore'] });
  const lines = readline.createInterface({ input: child.stdout });
  lines.on('line', line => receiver.ingest(line));
  child.on('error', () => receiver.clear());
  child.on('exit', () => { lines.close(); receiver.clear(); });
  return { stop() { lines.close(); child.kill(); receiver.clear(); } };
}

module.exports = { startSignalBridge };
