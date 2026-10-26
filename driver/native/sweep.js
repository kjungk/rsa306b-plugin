const MAX_POINTS = 4_194_304;

function finite(value, message) { if (!Number.isFinite(value)) throw new Error(message); return value; }

// No partial sweep is submitted to the SoundBase shell. Timing stays attached
// to lastMeasurement; the shell's uniform dBm array cannot express per-bin time.
export class SweepAssembler {
  constructor(config, serial) { this.config = config; this.serial = serial; this.pending = null; this.lastId = -1; }
  accept(record) {
    if (record.type === 'device') {
      if (record.serial !== this.serial || record.calibrated !== true) throw new Error('Native device/calibration identity changed.');
      return null;
    }
    if (record.type === 'sweep_begin') {
      if (!Number.isSafeInteger(record.sweep_id) || record.sweep_id <= this.lastId ||
          !Number.isInteger(record.segment_count) || record.segment_count < 1 || record.segment_count > 512 ||
          record.start_hz !== this.config.startHz || record.stop_hz !== this.config.stopHz) throw new Error('Invalid native sweep header.');
      this.lastId = record.sweep_id;
      this.pending = { id: record.sweep_id, expected: record.segment_count, segments: [], count: 0,
        begin: finite(record.host_begin_unix_seconds, 'Missing sweep time.'), bad: false, calibrationId: null };
      return null;
    }
    const pass = this.pending;
    if (!pass || record.sweep_id !== pass.id) throw new Error('Native sweep identity mismatch.');
    if (record.type === 'spectrum') {
      if (record.schema_version !== 2 || record.level_unit !== 'dBm' || record.calibrated !== true ||
          !record.calibration?.id || record.calibration.device_serial !== this.serial ||
          record.calibration.basis !== 'factory_tables' || record.calibration.algorithm !== 'hw7-fullrange-v1' ||
          record.calibration.absolute_accuracy_verified !== false || record.calibration.impedance_ohms !== 50) throw new Error('Only factory-corrected dBm spectra with device calibration provenance can enter SoundBase.');
      if (pass.calibrationId && pass.calibrationId !== record.calibration.id) throw new Error('Calibration changed inside a sweep.');
      pass.calibrationId = record.calibration.id;
      if (record.segment_index !== pass.segments.length || pass.segments.length >= pass.expected) throw new Error('Missing or reordered native segment.');
      const f = record.frequency_hz, y = record.levels;
      if (!Array.isArray(f) || !Array.isArray(y) || f.length < 2 || f.length !== y.length ||
          (pass.count += f.length) > MAX_POINTS) throw new Error('Invalid or oversized native spectrum arrays.');
      for (let i = 0; i < f.length; i++) {
        if (!Number.isFinite(f[i]) || !Number.isFinite(y[i]) || f[i] < 0 || (i && f[i] <= f[i-1])) throw new Error('Non-finite or unordered native bins.');
      }
      const quality = record.quality;
      if (!quality || ['overload','unlocked','power_failure','device_frame_drop','timestamp_regressed'].some(key => quality[key] !== false) ||
          !Number.isInteger(quality.raw_status) || quality.raw_status !== 0 || quality.gap_events_before_contiguous_block !== 0) pass.bad = true;
      finite(record.host_acquire_begin_unix_seconds, 'Missing acquisition time.');
      finite(record.host_read_end_unix_seconds, 'Missing read time.');
      pass.segments.push(record); return null;
    }
    if (record.type !== 'sweep_end') throw new Error('Unexpected worker record in sweep.');
    this.pending = null;
    if (record.complete !== true || record.segments_completed !== pass.expected || pass.segments.length !== pass.expected || pass.bad) {
      return { skipped: true, reason: 'Incomplete or quality-flagged sweep excluded. Last plotted trace is older.' };
    }
    const duration = finite(record.duration_seconds, 'Missing sweep duration.');
    if (duration < 0) throw new Error('Invalid sweep duration.');
    const { startHz, stopHz, pointCount } = this.config;
    const step = (stopHz-startHz)/(pointCount-1), values = Array(pointCount).fill(null);
    for (const segment of pass.segments) {
      for (let i = 0; i < segment.frequency_hz.length; i++) {
        const f = segment.frequency_hz[i];
        if (f < startHz || f > stopHz) continue;
        const column = Math.round((f-startHz)/step);
        values[column] = Math.max(values[column] ?? -Infinity, segment.levels[i]);
      }
    }
    if (values.some(value => value === null)) return { skipped: true, reason: 'Sweep has unobserved output bins. Reduce point count; missing data was not filled.' };
    return { values, measurement: { begin: pass.begin, durationSeconds: duration, calibrationId: pass.calibrationId,
      segmentTimes: pass.segments.map(r => ({ begin: r.host_acquire_begin_unix_seconds, end: r.host_read_end_unix_seconds,
        deviceTicks: r.device_timestamp_ticks, gapSeconds: r.quality.between_capture_gap_seconds })) } };
  }
}
