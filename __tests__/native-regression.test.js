import test from 'node:test';
import assert from 'node:assert/strict';
import { RSAAdapter, createDiscovery } from '../driver/native/adapter.js';
import { SweepAssembler } from '../driver/native/sweep.js';
import { requireCalibration } from '../driver/native/worker.js';

// Synthetic factory-corrected contract. Absolute accuracy is explicitly unverified.
const caps = { type: 'capabilities', schema_version: 1, calibrated: true, level_units: ['dBm'],
  spectrum_schema_versions: [2], algorithm: 'hw7-fullrange-v1', calibrated_min_hz: 9000, calibrated_max_hz: 6.2e9, min_rbw_hz: 161,
  calibration_basis: 'factory_tables', absolute_accuracy_verified: false };
const config = { startHz: 100, stopHz: 104, pointCount: 3 };
const quality = { raw_status: 0, overload: false, unlocked: false, power_failure: false,
  device_frame_drop: false, timestamp_regressed: false, gap_events_before_contiguous_block: 0 };
function records() {
  return [
    { schema_version: 1, type: 'device', serial: 'TEST', calibrated: true },
    { schema_version: 1, type: 'sweep_begin', sweep_id: 0, segment_count: 2, start_hz: 100, stop_hz: 104, host_begin_unix_seconds: 10 },
    ...[0,1].map(index => ({ schema_version: 2, type: 'spectrum', sweep_id: 0, segment_index: index,
      level_unit: 'dBm', calibrated: true, calibration: { id: 'synthetic-fixture', device_serial: 'TEST', basis: 'factory_tables', algorithm: 'hw7-fullrange-v1', absolute_accuracy_verified: false, impedance_ohms: 50 },
      frequency_hz: index ? [102,103,104] : [100,101,102], levels: index ? [-70,-65,-60] : [-80,-50,-90],
      host_acquire_begin_unix_seconds: 10+index, host_read_end_unix_seconds: 10.01+index, quality: { ...quality } })),
    { schema_version: 1, type: 'sweep_end', sweep_id: 0, complete: true, segments_completed: 2, duration_seconds: 1.01 },
  ];
}

test('calibration gate runs before any discovery or USB probe', async () => {
  assert.throws(() => requireCalibration({ ...caps, calibrated: false }), /calibration/);
  const calls = [], io = { query: async args => { calls.push(args[0]); return { ...caps, level_units: ['dBFS'] }; } };
  await assert.rejects(new RSAAdapter({config:{serial:'TEST'}},io).open(), /calibration/);
  await assert.rejects(createDiscovery(io)(), /calibration/);
  assert.deepEqual(calls,['capabilities','capabilities']);
});
test('only complete calibrated sweeps reach the uniform peak axis with timing retained', () => {
  const assembler = new SweepAssembler(config,'TEST');
  let result;
  for (const record of records()) result = assembler.accept(record);
  assert.deepEqual(result.values,[-80,-50,-60]);
  assert.equal(result.measurement.durationSeconds,1.01);
  assert.deepEqual(result.measurement.segmentTimes.map(s => s.begin),[10,11]);
});
test('flagged, incomplete and missing output bins never become a measurement', () => {
  for (const change of [r => { r[2].quality.overload = true; }, r => { r[4].complete = false; },
      r => { r[2].frequency_hz = [100,100.1,100.2]; r[3].frequency_hz = [103.8,103.9,104]; }]) {
    const input = records(); change(input);
    const assembler = new SweepAssembler({ ...config, pointCount: 5 },'TEST');
    let result; for (const record of input) result = assembler.accept(record);
    assert.equal(result.skipped,true); assert.equal(result.values,undefined);
  }
});
test('wrong units, calibration identity, reordered segments and nonfinite values fail closed', () => {
  for (const change of [r => { r[2].level_unit = 'dBFS'; }, r => { r[2].calibration.device_serial = 'OTHER'; },
      r => { r[2].calibration.basis = 'unknown'; }, r => { r[2].calibration.absolute_accuracy_verified = true; }, r => { r[3].calibration.id = 'changed'; }, r => { r[2].segment_index = 1; },
      r => { r[2].levels[0] = NaN; }, r => { r[2].frequency_hz[1] = 99; }]) {
    const input = records(); change(input); const assembler = new SweepAssembler(config,'TEST');
    assert.throws(() => { for (const record of input) assembler.accept(record); });
  }
});
function fakeIO() {
  const events = [], calls = [];
  return { events, calls,
    query: async args => { calls.push(args); switch (args[0]) {
      case 'capabilities': return caps;
      case 'probe': return { type: 'device', model: 'RSA306B', serial: 'TEST', calibrated: true };
      case 'discover': return { type: 'discovery', devices: [{ bus: 1, address: 2 }] };
      case 'plan-sweep': return { type: 'sweep_plan', segments: [{}] };
      default: throw new Error('Unexpected command');
    } },
    stream: () => { events.push('start'); return { stop: async () => {
      events.push('stop'); await new Promise(resolve => setTimeout(resolve,5)); events.push('exit');
    } }; },
  };
}
test('reconfigure awaits old process exit and restarts; close stops the replacement', async () => {
  const io = fakeIO(), adapter = new RSAAdapter({ id: 'usb:rsa306b:TEST' },io);
  await adapter.open(); await adapter.applyConfig({}); await adapter.startSweep(() => {});
  await adapter.applyConfig({ stopHz: 698e6 }); await adapter.close();
  assert.deepEqual(io.events,['start','stop','exit','start','stop','exit']);
  assert.equal(adapter.opened,false);
  assert.ok(io.calls.filter(c => c[0] === 'plan-sweep').every(c => c.includes('dbm')));
});
test('out-of-range settings clamp to hardware limits and echo the accepted values', async () => {
  const io = fakeIO(), adapter = new RSAAdapter({config:{serial:'TEST'}},io); await adapter.open();
  const applied = await adapter.applyConfig({startHz:0,stopHz:7e9,pointCount:5000,rbwHz:0,controls:{attenuationDb:99,ifGainDb:-100}});
  assert.equal(applied.startHz,9000); assert.equal(applied.stopHz,6.2e9); assert.equal(applied.pointCount,2000);
  assert.equal(applied.controls.attenuationDb,31); assert.equal(applied.controls.ifGainDb,-11);
  assert.ok(applied.resolved.rbwHz>0); assert.deepEqual(io.events,[]); await adapter.close();
});
test('discovery caches identity instead of repeatedly initializing hardware', async () => {
  const io = fakeIO(), discover = createDiscovery(io);
  assert.equal((await discover())[0].id,'usb:rsa306b:TEST'); await discover();
  assert.equal(io.calls.filter(c => c[0] === 'probe').length,1);
});

test('RBW reports actual Hann ENBW and automatic choice without relabeling it', async () => {
  const io = fakeIO(), adapter = new RSAAdapter({config:{serial:'TEST'}},io);
  const opened = await adapter.open();
  assert.ok(opened.capabilities.rbwHz.includes(5126.953125));
  const auto = await adapter.applyConfig({});
  assert.equal(auto.rbwHz,undefined); assert.equal(auto.resolved.rbwHz,5126.953125);
  const requested = await adapter.applyConfig({ rbwHz: 10000 });
  assert.equal(requested.rbwHz,5126.953125);
  const narrow = await adapter.applyConfig({ startHz: 594e6, stopHz: 594.01e6 });
  assert.equal(narrow.resolved.rbwHz,2563.4765625);
  await adapter.close();
});

test('native settings reject malformed numbers, ignore Windows controls, and require explicit matching serial',async()=>{
 const io=fakeIO();
 await assert.rejects(new RSAAdapter({},io).open(),/serial/);
 await assert.rejects(new RSAAdapter({id:'usb:rsa306b:TEST',config:{serial:'OTHER'}},io).open(),/serial/);
 const a=new RSAAdapter({config:{serial:'TEST'}},io);await a.open();
 try {
  for(const request of [{pointCount:NaN},{rbwHz:Infinity},{controls:{ifGainDb:'loud'}}])await assert.rejects(a.applyConfig(request),/finite/);
  const c=await a.applyConfig({controls:{refLevelDbm:-20}});assert.deepEqual(c.controls,{attenuationDb:.5,ifGainDb:11});
 }finally{await a.close();}
});
