#!/usr/bin/env node
// Explicit synthetic smoke; never opens USB. --real-worker exercises the actual
// platform worker in mock mode; --boot-only does not discover/acquire at all.
import { spawn, execFile } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { setTimeout as delay } from 'node:timers/promises';

const flags = new Set(process.argv.slice(2));
for (const flag of flags) {
  if (!['--boot-only', '--real-worker'].includes(flag)) throw new Error(`Unknown option: ${flag}`);
}
if (flags.size > 1) throw new Error('Choose either --boot-only or --real-worker.');
const bootOnly = flags.has('--boot-only');
const env = { ...process.env, SB_PLUGIN_TOKEN: randomBytes(24).toString('hex') };
delete env.SB_RSA306B_MOCK;
delete env.SB_RSA306B_NATIVE_MOCK;
if (!bootOnly) env[flags.has('--real-worker') ? 'SB_RSA306B_NATIVE_MOCK' : 'SB_RSA306B_MOCK'] = '1';
delete env.SB_PLUGIN_LIFELINE; // Not part of the public core 1.4 SDK.
const child = spawn(process.execPath, [fileURLToPath(new URL('../main.js', import.meta.url))], {
  env, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true,
});
let output = '', stderr = '', exited = false;
const done = new Promise(resolve => child.once('close', () => { exited = true; resolve(); }));
child.stderr.on('data', chunk => { stderr = (stderr + chunk).slice(-8192); });
const ok = message => process.stdout.write(`  ok    ${message}\n`);
let base, devicePath;
async function request(method, path, body, authenticated = true) {
  const response = await fetch(base + path, {
    method, signal: AbortSignal.timeout(15_000),
    headers: {
      ...(authenticated ? { authorization: `Bearer ${env.SB_PLUGIN_TOKEN}` } : {}),
      ...(body === undefined ? {} : { 'content-type': 'application/json' }),
    },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  const text = await response.text();
  if (!response.ok) throw new Error(`${method} ${path}: ${response.status} ${text.slice(0, 500)}`);
  return text ? JSON.parse(text) : null;
}
try {
  const port = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('No startup handshake within the host limit of 10 seconds.')), 10_000);
    const fail = error => { clearTimeout(timer); reject(error); };
    child.once('error', fail);
    child.once('exit', code => fail(new Error(`Plugin exited during startup (${code}): ${stderr}`)));
    child.stdout.on('data', chunk => {
      output = (output + chunk).slice(-64 * 1024);
      const line = output.split('\n').find(value => value.startsWith('SB_PLUGIN_READY '));
      if (!line) return;
      try {
        const value = JSON.parse(line.slice('SB_PLUGIN_READY '.length)).port;
        if (!Number.isInteger(value) || value < 1 || value > 65535) throw new Error('Invalid handshake port');
        clearTimeout(timer); resolve(value);
      } catch (error) { fail(error); }
    });
  });
  base = `http://127.0.0.1:${port}`;
  ok('startup handshake within 10 seconds');
  if (!(await request('GET', '/health', undefined, false)).ok) throw new Error('Health check failed');
  const denied = await fetch(base + '/info', { signal: AbortSignal.timeout(5000) });
  if (denied.status !== 401) throw new Error('Private routes accepted a missing bearer token');
  const info = await request('GET', '/info');
  if (info.id !== 'rsa306b' || info.contract.core !== '1.4') throw new Error('Wrong plugin identity or contract');
  await request('PUT', '/config', { values: {} });
  ok('health, authenticated API and initialization');
  if (bootOnly) {
    ok('boot-only: no discovery or acquisition requested');
  } else {
    const deadline = Date.now() + 15_000;
    let devices = [];
    while (!devices.length && Date.now() < deadline) {
      ({ devices } = await request('GET', '/devices'));
      if (!devices.length) await delay(100);
    }
    if (devices.length !== 1) throw new Error('Expected exactly one explicit mock device; acquisition must not be skipped');
    devicePath = `/devices/${encodeURIComponent(devices[0].id)}`;
    const applied = await request('POST', devicePath + '/configuration', {
      startHz: 590e6, stopHz: 600e6, pointCount: 401, rbwHz: 10000,
    });
    await request('POST', devicePath + '/sweep/start');
    let trace;
    const traceDeadline = Date.now() + 15_000;
    while (!trace && Date.now() < traceDeadline) {
      try { trace = await request('GET', devicePath + '/trace'); }
      catch (error) { if (!error.message.includes(': 409 ')) throw error; await delay(100); }
    }
    if (!trace || trace.unit !== 'dBm' || trace.pointCount !== applied.pointCount ||
        trace.startHz !== applied.startHz || trace.stopHz !== applied.stopHz ||
        trace.amplitudesDbm?.length !== applied.pointCount || !trace.amplitudesDbm.every(Number.isFinite)) {
      throw new Error('Mock acquisition returned invalid units, values or trace geometry');
    }
    ok(`explicit mock acquisition: ${trace.pointCount} finite dBm points`);
    await request('POST', devicePath + '/sweep/stop');
    await request('DELETE', devicePath);
    devicePath = null;
    ok('stop and device cleanup');
  }
} catch (error) {
  process.stderr.write(`FAILED: ${error.message}\n${stderr}`);
  process.exitCode = 1;
} finally {
  if (devicePath) {
    try { await request('DELETE', devicePath); } catch { /* Continue bounded process cleanup. */ }
  }
  if (!exited) child.kill('SIGTERM');
  await Promise.race([done, delay(6000, undefined, { ref: false })]);
  if (!exited) {
    if (process.platform === 'win32') {
      await new Promise(resolve => execFile('taskkill', ['/pid', String(child.pid), '/f', '/t'], resolve));
    } else child.kill('SIGKILL');
    await done;
    process.stderr.write('FAILED: plugin required forced shutdown\n');
    process.exitCode = 1;
  }
}
