// Contract tests exercise the actual SoundBase shell and a synthetic child worker.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
process.env.SB_RSA306B_MOCK = '1';
delete process.env.SB_RSA306B_NATIVE_MOCK;
process.env.SB_PLUGIN_TOKEN = 'rsa306b-contract-test';
const handle = await (await import('../main.js')).default;
const manifest = JSON.parse(readFileSync(new URL('../soundbase-plugin.json', import.meta.url), 'utf8'));
const ID = 'usb:rsa306b:MOCK-RSA306B', path = `/devices/${encodeURIComponent(ID)}`;
const request = async (method, route, body, authorized = true) => {
  const res = await fetch(`${handle.url}${route}`, { method,
    headers: { ...(authorized ? { authorization: `Bearer ${process.env.SB_PLUGIN_TOKEN}` } : {}), ...(body === undefined ? {} : { 'content-type': 'application/json' }) },
    body: body === undefined ? undefined : JSON.stringify(body) });
  const text = await res.text();
  return { status: res.status, body: text ? JSON.parse(text) : null };
};
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
test.after(async () => { await handle.close(); });

test('valid manifest, authentication, lazy diagnostics and stable discovery', async () => {
  assert.equal(handle.manifest.id, 'rsa306b');
  assert.equal((await request('GET', '/health', undefined, false)).body.ok, true);
  assert.equal((await request('GET', '/info', undefined, false)).status, 401);
  assert.equal((await request('PUT', '/config', { values: {} })).status, 204);
  const first = await request('GET', '/devices');
  const second = await request('GET', '/devices');
  assert.equal(first.status, 200); assert.equal(first.body.devices[0].id, ID);
  assert.equal(first.body.devices[0].product, 'plugin:rsa306b/rsa306b');
  assert.equal(second.body.devices[0].id, ID);
  assert.ok(manifest.products.some(p => p.deviceTypeId === first.body.devices[0].product));
});
test('full range clamps, effective echo, trace geometry and shell hold modes', async () => {
  const applied = await request('POST', `${path}/configuration`, { startHz: 0, stopHz: 7e9, pointCount: 5000, rbwHz: 10000,
    controls: { attenuationDb: 100, ifGainDb: -100 } });
  assert.equal(applied.status, 200, JSON.stringify(applied.body));
  assert.equal(applied.body.startHz, 9000); assert.equal(applied.body.stopHz, 6.2e9); assert.equal(applied.body.pointCount, 2000);
  assert.deepEqual(applied.body.controls, { attenuationDb: 31, ifGainDb: -11 });
  const read = await request('GET', `${path}/configuration`); assert.deepEqual(read.body, applied.body);
  const devices = (await request('GET', '/devices')).body.devices;
  assert.deepEqual([...devices[0].capabilities.traceModes].sort(), ['average', 'clear-write', 'max-hold', 'min-hold']);
  assert.equal((await request('POST', `${path}/sweep/start`)).status, 200);
  assert.equal((await request('POST', `${path}/sweep/start`)).status, 200);
  const first = (await request('GET', `${path}/trace`)).body;
  const second = (await request('GET', `${path}/trace`)).body;
  assert.equal(first.amplitudesDbm.length, first.pointCount); assert.ok(first.amplitudesDbm.every(Number.isFinite));
  assert.equal(first.stepHz, (first.stopHz-first.startHz)/(first.pointCount-1)); assert.equal(first.unit, 'dBm');
  assert.ok(second.sweepId > first.sweepId);
  await request('POST', `${path}/configuration`, { traceMode: 'max-hold' });
  const heldA = (await request('GET', `${path}/trace`)).body, heldB = (await request('GET', `${path}/trace`)).body;
  assert.ok(heldB.amplitudesDbm.every((v,i) => v >= heldA.amplitudesDbm[i]));
  await request('POST', `${path}/sweep/stop`); await request('POST', `${path}/sweep/stop`);
});
test('partial HTTP config retains manual RBW and controls; null returns to auto', async () => {
  const manual = await request('POST', `${path}/configuration`, { startHz:470e6, stopHz:616e6, pointCount:401, rbwHz:10000, controls:{attenuationDb:10,ifGainDb:11} });
  assert.equal(manual.status,200);
  const patch = await request('POST', `${path}/configuration`, { stopHz:620e6, controls:{attenuationDb:12} });
  assert.equal(patch.body.rbwHz,manual.body.rbwHz); assert.equal(patch.body.controls.ifGainDb,11); assert.equal(patch.body.controls.attenuationDb,12);
  const auto = await request('POST', `${path}/configuration`, { rbwHz:null });
  assert.equal(auto.body.rbwHz,undefined); assert.ok(auto.body.resolved.rbwHz>0);
});
test('a worker crash fails its device while the shell stays healthy and can reopen', async () => {
  await request('POST', `${path}/sweep/start`);
  const entry = handle.registry.devices.get(ID); const child = entry.adapter.child;
  process.kill(child.pid, 'SIGKILL'); await child.done;
  for (let i=0;i<50 && entry.status.status!=='failed';i++) await delay(10);
  assert.equal(entry.status.status,'failed'); assert.equal((await request('GET','/health')).body.ok,true);
  // Fatal teardown is asynchronous in the public shell.
  await delay(30);
  const reopened = await request('POST',`${path}/configuration`,{startHz:470e6,stopHz:616e6});
  assert.equal(reopened.status,200,JSON.stringify(reopened.body));
  assert.equal((await request('DELETE',path)).status,204);
});
