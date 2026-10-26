// Explicit hardware-free child fixture. Production never selects this worker.
const args = process.argv.slice(2), command = args[0];
const option = (key, fallback) => { const i = args.indexOf(key); return i < 0 ? fallback : args[i + 1]; };
const num = (key, fallback) => Number(option(key, fallback));
const emit = value => process.stdout.write(`${JSON.stringify(value)}\n`);
const serial = 'MOCK-RSA306B';
const identity = { type: 'device', schema_version: 1, model: 'RSA306B', serial, calibrated: true, synthetic: true, firmware: 'mock' };
const start = num('--start-hz', 470e6), stop = num('--stop-hz', 694e6);
if (option('--serial', serial) !== serial) throw new Error('Mock serial does not match the selected analyzer.');
if (command === 'capabilities') {
  emit({ type: 'capabilities', schema_version: 1, calibrated: true, level_units: ['dBm'], spectrum_schema_versions: [2],
    calibration_basis: 'factory_tables', algorithm: 'hw7-fullrange-v1', calibrated_min_hz: 9000, calibrated_max_hz: 6.2e9,
    min_rbw_hz: 1.5 * 112e6 / 1048576, absolute_accuracy_verified: false, synthetic: true });
} else if (command === 'discover') emit({ type: 'discovery', devices: [{ bus: 1, address: 1 }] });
else if (command === 'probe') emit(identity);
else if (command === 'plan-sweep') emit({ type: 'sweep_plan', segments: [{ start_hz: start, stop_hz: stop }] });
else if (command === 'sweep') {
  emit(identity);
  let sweep = 0, timer, closing = false;
  const pass = () => {
    if (closing) return;
    const begin = Date.now() / 1000, count = 4001;
    emit({ type: 'sweep_begin', sweep_id: sweep, segment_count: 1, start_hz: start, stop_hz: stop, host_begin_unix_seconds: begin });
    const frequency_hz = Array.from({ length: count }, (_, i) => start + i * (stop - start) / (count - 1));
    emit({ type: 'spectrum', schema_version: 2, sweep_id: sweep, segment_index: 0, level_unit: 'dBm', calibrated: true,
      calibration: { id: 'synthetic-mock', device_serial: serial, basis: 'factory_tables', algorithm: 'hw7-fullrange-v1', absolute_accuracy_verified: false, impedance_ohms: 50 },
      frequency_hz, levels: frequency_hz.map((f, i) => -100 + Math.sin(i / 19 + sweep) + 60 * Math.exp(-Math.pow((f - (start + .4 * (stop-start))) / ((stop-start)/100), 2))),
      host_acquire_begin_unix_seconds: begin, host_read_end_unix_seconds: begin + .01, device_timestamp_ticks: String(sweep + 1),
      quality: { raw_status: 0, overload: false, unlocked: false, power_failure: false, device_frame_drop: false, timestamp_regressed: false, gap_events_before_contiguous_block: 0, between_capture_gap_seconds: .04 } });
    emit({ type: 'sweep_end', sweep_id: sweep++, complete: true, segments_completed: 1, duration_seconds: .01 });
    timer = setTimeout(pass, 40);
  };
  const shutdown = () => { if (closing) return; closing = true; clearTimeout(timer); emit({ type: 'stopped' }); process.exit(0); };
  process.stdin.on('data', shutdown); process.stdin.on('end', shutdown); process.on('SIGTERM', shutdown);
  pass();
} else throw new Error('Unknown mock command.');
