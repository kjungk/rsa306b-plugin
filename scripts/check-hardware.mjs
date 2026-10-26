#!/usr/bin/env node
// Explicit opt-in manual acceptance client for an already extracted candidate.
// It never installs the plugin or changes a SoundBase project. Physical unplug,
// Desktop rendering and generator accuracy remain separate acceptance gates.
import { spawn, execFile } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { randomBytes } from 'node:crypto';
import assert from 'node:assert/strict';

const [folder, reportFile, optIn] = process.argv.slice(2);
if (!folder || !reportFile || optIn !== '--hardware') {
  throw new Error('Usage: node scripts/check-hardware.mjs EXTRACTED_PLUGIN NEW_REPORT.json --hardware (real acquisition; close other analyzer apps first)');
}
const root = resolve(folder), output = resolve(reportFile);
// Reserve a fresh evidence path before hardware access; never overwrite a report.
writeFileSync(output, '', { flag: 'wx' });
const pkg = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
const token = randomBytes(24).toString('hex');
const env = { ...process.env, SB_PLUGIN_TOKEN: token };
for (const name of ['SB_RSA306B_MOCK', 'SB_RSA306B_NATIVE_MOCK', 'SB_RSA306B_BACKEND', 'SB_PLUGIN_LIFELINE']) delete env[name];
const report = {
  schema_version: 1, status: 'partial', automated_status: 'running',
  target: `${process.platform}-${process.arch}`, node_version: process.version,
  plugin_version: pkg.version, content_hash: pkg.soundbase?.contentHash,
  source_commit: pkg.soundbase?.commit, started_at: new Date().toISOString(),
  stages: [], pending: ['physical unplug/reconnect', '30-minute soak', 'Desktop plot/waterfall visual check', 'signal-generator measurements', 'controlled hard-host-crash recovery'],
};
const child = spawn(process.execPath, [join(root, 'main.js')], { cwd: root, env, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
let stderr = '', logs = '', api, devicePath, exited = false, lastSweepId = -1;
const done = new Promise(resolveExit => child.once('close', () => { exited = true; resolveExit(); }));
child.stderr.on('data', chunk => { stderr = (stderr + chunk).slice(-16000); });
try {
  const port = await new Promise((resolvePort, reject) => {
    const timer = setTimeout(() => reject(new Error('No shell handshake within 10 seconds')), 10000);
    child.once('error', error => { clearTimeout(timer); reject(error); });
    child.once('exit', code => { clearTimeout(timer); reject(new Error(`Plugin exited (${code}): ${stderr}`)); });
    child.stdout.on('data', chunk => {
      logs = (logs + chunk).slice(-64000);
      const line = logs.split('\n').find(value => value.startsWith('SB_PLUGIN_READY '));
      if (line) { clearTimeout(timer); resolvePort(JSON.parse(line.slice(16)).port); }
    });
  });
  api = async (path, method = 'GET', body) => {
    const response = await fetch(`http://127.0.0.1:${port}${path}`, {
      method, signal: AbortSignal.timeout(90000),
      headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    const text = await response.text();
    if (!response.ok) throw new Error(`${method} ${path}: ${response.status} ${text.slice(0, 1000)}`);
    return text ? JSON.parse(text) : null;
  };
  await api('/config', 'PUT', { values: {} });
  const { devices } = await api('/devices');
  assert.equal(devices.length, 1, 'Connect exactly one supported RSA306B');
  report.device_id = devices[0].id;
  assert.ok(!devices[0].id.includes('MOCK'), 'Hardware check must never report a mock device');
  devicePath = `/devices/${encodeURIComponent(devices[0].id)}`;
  const stage = async (name, config, count, start = false) => {
    const begin = Date.now();
    const effective = await api(devicePath + '/configuration', 'POST', config);
    if (start) await api(devicePath + '/sweep/start', 'POST');
    // Configuration resets accumulation/trace counters. Check progression within
    // each stage and the newly applied axis before accepting a trace.
    lastSweepId = -1;
    const samples = [];
    while (samples.length < count) {
      if (Date.now() - begin > 90000) throw new Error(`Stage ${name} exceeded 90 seconds`);
      let trace;
      try { trace = await api(devicePath + '/trace'); }
      catch (error) {
        if (!error.message.includes(': 409 ')) throw error;
        await new Promise(resolveTimer => setTimeout(resolveTimer, 100)); continue;
      }
      assert.equal(trace.startHz, effective.startHz); assert.equal(trace.stopHz, effective.stopHz);
      assert.equal(trace.pointCount, effective.pointCount); assert.equal(trace.unit, 'dBm');
      assert.equal(trace.amplitudesDbm.length, effective.pointCount);
      assert.ok(trace.amplitudesDbm.every(Number.isFinite));
      assert.ok(trace.sweepId > lastSweepId); lastSweepId = trace.sweepId;
      samples.push({ sweep_id: trace.sweepId, minimum_dbm: Math.min(...trace.amplitudesDbm), maximum_dbm: Math.max(...trace.amplitudesDbm) });
    }
    report.stages.push({ name, status: 'passed', effective, traces: samples, duration_ms: Date.now() - begin });
    console.log(`passed: ${name} (${samples.length} traces)`);
  };
  await stage('full-range', { startHz: 9000, stopHz: 6.2e9, pointCount: 1001, rbwHz: 10000 }, 3, true);
  await stage('microphone-band', { startHz: 470e6, stopHz: 694e6, pointCount: 1001, rbwHz: 10000 }, 5);
  await stage('LF-only', { startHz: 9000, stopHz: 1e6, pointCount: 401, rbwHz: 1000 }, 3);
  await stage('LF-RF-boundary', { startHz: 21.75e6, stopHz: 22.25e6, pointCount: 401, rbwHz: 1000 }, 3);
  await stage('active-retune', { startHz: 590e6, stopHz: 600e6, pointCount: 1001, rbwHz: 10000 }, 3);
  if (process.platform === 'win32') await stage('reference-level', { controls: { refLevelDbm: -10 } }, 3);
  await api(devicePath + '/sweep/stop', 'POST');
  await stage('fresh-worker-restart', { startHz: 590e6, stopHz: 600e6 }, 3, true);
  await api(devicePath + '/sweep/stop', 'POST');
  const final = await api('/devices'); report.final_device = final.devices.find(device => device.id === devices[0].id);
  await api(devicePath, 'DELETE'); devicePath = null;
  report.automated_status = 'passed';
} catch (error) {
  report.status = 'failed'; report.automated_status = 'failed'; report.error = error.message;
  process.stderr.write(`${error.message}\n`); process.exitCode = 1;
} finally {
  if (devicePath && api) {
    try { await api(devicePath, 'DELETE'); } catch (error) { report.cleanup_error = error.message; }
  }
  if (!exited) child.kill();
  let timer;
  await Promise.race([done, new Promise(resolveTimer => { timer = setTimeout(resolveTimer, 15000); })]);
  clearTimeout(timer);
  if (!exited) {
    if (process.platform === 'win32') await new Promise(resolveKill => execFile('taskkill', ['/pid', String(child.pid), '/f', '/t'], resolveKill));
    else child.kill('SIGKILL');
    report.cleanup_error = 'Plugin required forced shutdown';
  }
  if (report.cleanup_error) { report.status = 'failed'; report.automated_status = 'failed'; process.exitCode = 1; }
  report.finished_at = new Date().toISOString();
  if (stderr) report.stderr = stderr;
  if (report.automated_status !== 'passed') report.plugin_log = logs;
  writeFileSync(output, JSON.stringify(report, null, 2) + '\n');
  console.log(`Hardware evidence: ${output}; remaining gates are explicitly pending.`);
}
