#!/usr/bin/env node
// Executes only explicitly requested mock workers. Never opens hardware.
import { spawn } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const root = resolve(process.argv[2] ?? fileURLToPath(new URL('..', import.meta.url)));
const target = `${process.platform}-${process.arch}`;
const portable = process.argv.includes('--portable');
if (!portable && !['darwin-arm64', 'win32-x64'].includes(target)) {
  throw new Error(`Real worker smoke requires a supported target, received ${target}; use --portable only for assembly smoke.`);
}
const metadata = JSON.parse(readFileSync(join(root, 'soundbase-plugin.json'), 'utf8'));
const token = 'rsa306b-package-check';
async function check(mock, windowsBackend = 'native') {
  const env = { ...process.env, SB_PLUGIN_TOKEN: token };
  delete env.SB_RSA306B_MOCK;
  delete env.SB_RSA306B_NATIVE_MOCK;
  delete env.SB_RSA306B_BACKEND;
  if (mock) {
    env.SB_RSA306B_MOCK = '1';
    if (!portable) env.SB_RSA306B_NATIVE_MOCK = '1';
  }
  const child = spawn(process.execPath, [join(root, 'main.js')], {
    cwd: root, env, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true,
  });
  let logs = '', devicePath;
  const exited = new Promise(resolveExit => child.once('exit', (code, signal) => resolveExit({ code, signal })));
  child.stderr.on('data', chunk => { logs = (logs + chunk).slice(-16000); });
  let api;
  try {
    const port = await new Promise((resolvePort, reject) => {
      let stdout = '';
      const timer = setTimeout(() => reject(new Error('No handshake within the host deadline of 10 seconds')), 10000);
      const done = value => { clearTimeout(timer); resolvePort(value); };
      child.once('error', error => { clearTimeout(timer); reject(error); });
      child.once('exit', () => { clearTimeout(timer); reject(new Error('Plugin exited before handshake: ' + logs)); });
      child.stdout.on('data', chunk => {
        stdout += chunk;
        const line = stdout.split('\n').find(line => line.startsWith('SB_PLUGIN_READY '));
        if (line) done(JSON.parse(line.slice('SB_PLUGIN_READY '.length)).port);
      });
    });
    api = async (path, method = 'GET', body, authenticated = true) => {
      const response = await fetch(`http://127.0.0.1:${port}${path}`, {
        method, headers: { ...(authenticated ? { authorization: `Bearer ${token}` } : {}), 'content-type': 'application/json' },
        ...(body === undefined ? {} : { body: JSON.stringify(body) }), signal: AbortSignal.timeout(20000),
      });
      const text = await response.text();
      if (!response.ok) throw new Error(`${method} ${path}: ${response.status} ${text}`);
      return text ? JSON.parse(text) : null;
    };
    assert.equal((await api('/health')).ok, true);
    assert.equal((await api('/info')).id, metadata.id);
    await assert.rejects(api('/info', 'GET', undefined, false), /401/);
    // A clean boot check deliberately does not request enumeration or initialize
    // drivers: it proves package installation does not require attached hardware.
    if (mock) {
      await api('/config', 'PUT', { values: { windowsBackend } });
      let devices = [];
      for (let i = 0; i < 40 && !devices.length; i++) {
        devices = (await api('/devices')).devices;
        if (!devices.length) await new Promise(resolveTimer => setTimeout(resolveTimer, 100));
      }
      assert.equal(devices.length, 1, 'The explicitly enabled worker mock must discover one device');
      devicePath = `/devices/${encodeURIComponent(devices[0].id)}`;
      const config = await api(`${devicePath}/configuration`, 'POST', {
        startHz: 9000, stopHz: 6.2e9, pointCount: 401, rbwHz: 10000,
      });
      assert.equal(config.startHz, 9000);
      assert.equal(config.stopHz, 6.2e9);
      assert.ok(windowsBackend === 'tektronix-api'
        ? Number.isFinite(config.controls.refLevelDbm)
        : Number.isFinite(config.controls.attenuationDb));
      await api(`${devicePath}/sweep/start`, 'POST');
      let trace;
      for (let i = 0; i < 50 && !trace; i++) {
        try { trace = await api(`${devicePath}/trace`); }
        catch (error) { if (!error.message.includes('409')) throw error; }
        if (!trace) await new Promise(resolveTimer => setTimeout(resolveTimer, 100));
      }
      assert.ok(trace, 'The mock must exercise the complete acquisition path');
      assert.equal(trace.amplitudesDbm.length, config.pointCount);
      assert.ok(trace.amplitudesDbm.every(Number.isFinite));
      assert.equal(trace.startHz, config.startHz);
      assert.equal(trace.stopHz, config.stopHz);
      await api(`${devicePath}/sweep/stop`, 'POST');
      await api(devicePath, 'DELETE');
      devicePath = null;
    }
  } finally {
    if (devicePath && api) await api(devicePath, 'DELETE').catch(() => {});
    child.kill();
    let exitTimer;
    const result = await Promise.race([exited, new Promise(resolveTimer => {
      exitTimer = setTimeout(() => resolveTimer(null), 12000);
    })]);
    clearTimeout(exitTimer);
    if (!result) {
      if (process.platform === 'win32') {
        await new Promise(resolveKill => spawn('taskkill', ['/pid', String(child.pid), '/f', '/t'], { windowsHide: true }).once('exit', resolveKill));
      } else child.kill('SIGKILL');
      throw new Error('Packaged plugin required forced shutdown: ' + logs);
    }
  }
}
await check(false);
await check(true);
if (!portable && target === 'win32-x64') await check(true, 'tektronix-api');
console.log(JSON.stringify({ status: 'passed', target, mode: portable ? 'portable-mock' : 'real-worker-mock',
  backends: !portable && target === 'win32-x64' ? ['native', 'tektronix-api'] : ['native'],
  plugin_id: metadata.id, version: metadata.version }));
