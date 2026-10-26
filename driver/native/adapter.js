import { query, stream, requireCalibration } from './worker.js';
import { SweepAssembler } from './sweep.js';
import { clamp, finiteOr, normalizeRange, selectedSerial, deviceDescriptor, validateRequest } from '../settings.js';

export const PRODUCT = 'plugin:rsa306b/rsa306b';
const active = new Map();
let opening = 0, discoveryTask;
const warnings = message => [
  { id: 'factory-calibration', severity: 'warning', message: 'Factory-corrected dBm. Full-range conversion and absolute RF accuracy still require independent hardware qualification.' },
  { id: 'sequential-captures', severity: 'info', message },
];
const controls = [
  { id: 'attenuationDb', type: 'number', label: 'RF attenuation', unit: 'dB', min: 0, max: 31, step: .5, default: .5 },
  { id: 'ifGainDb', type: 'number', label: 'IF gain', unit: 'dB', min: -11, max: 20, step: .5, default: 11 },
];

export class RSAAdapter {
  constructor(device = {}, io = { query, stream }) {
    this.device = device; this.io = io; this.child = null; this.config = null;
    this.opened = false; this.onFatal = null; this.onWarnings = null; this.lastMeasurement = null;
  }
  async open() {
    if (discoveryTask) await discoveryTask.catch(() => {});
    if (this.opened) return this.openInfo;
    if (this.opening || opening || active.size) throw new Error('The RSA306B is already open in this plugin. Close its other live scan first.');
    const serial = selectedSerial(this.device);
    this.opening = true; opening++;
    try {
      this.caps = requireCalibration(await this.io.query(['capabilities']));
      const identity = await this.io.query(['probe', '--serial', serial, '--level-unit', 'dbm']);
      if (identity.type !== 'device' || identity.model !== 'RSA306B' || identity.serial !== serial || identity.calibrated !== true) {
        throw new Error('Connected analyzer differs from the selected factory-corrected RSA306B.');
      }
      this.serial = identity.serial; this.opened = true; active.set(this.serial, deviceDescriptor(this.serial));
      this.openInfo = { identity: { model: identity.model, serial: identity.serial, firmware: identity.firmware, name: `RSA306B ${identity.serial}` },
        capabilities: { minFrequencyHz: this.caps.calibrated_min_hz, maxFrequencyHz: this.caps.calibrated_max_hz,
          minPointCount: 2, maxPointCount: 2000, defaultPointCount: 1001,
          rbwHz: Array.from({ length: 14 }, (_, i) => 1.5 * 112e6 / (2 ** (20 - i))), controls } };
      return this.openInfo;
    } finally { this.opening = false; opening--; }
  }
  async applyConfig(request = {}) {
    if (!this.opened) throw new Error('Open the analyzer first.');
    validateRequest(request, ['attenuationDb', 'ifGainDb']);
    const prior = this.config ?? { startHz: 470e6, stopHz: 694e6, pointCount: 1001, controls: { attenuationDb: .5, ifGainDb: 11 } };
    const range = normalizeRange(request, prior, this.caps.calibrated_min_hz, this.caps.calibrated_max_hz, 3 * this.caps.min_rbw_hz);
    const cfg = { ...range, pointCount: clamp(Math.round(finiteOr(request.pointCount, prior.pointCount)), 2, 2000), controls: {} };
    for (const control of controls) {
      cfg.controls[control.id] = clamp(Math.round(finiteOr(request.controls?.[control.id], prior.controls[control.id]) * 2) / 2, control.min, control.max);
    }
    // The shell already merges desired configuration. Missing RBW here means
    // auto, including after an explicit null clears a previous manual value.
    const automatic = !Number.isFinite(request.rbwHz);
    const target = clamp(automatic ? Math.min(10000, (cfg.stopHz - cfg.startHz) / 3) : request.rbwHz,
      this.caps.min_rbw_hz, Math.min(1.5 * 112e6 / 128, (cfg.stopHz - cfg.startHz) / 3));
    let n = 8;
    while (1.5 * 112e6 / n > target && n < 1048576) n *= 2;
    let plan;
    for (;;) {
      cfg.rbwHz = 1.5 * 112e6 / n;
      try { plan = await this.io.query(['plan-sweep', ...this.arguments(cfg)]); break; }
      catch (error) {
        if (error.code !== 'sweep_too_large' || n <= 128) throw error;
        n /= 2; // Clamp to the finest RBW that fits the native complete-sweep budget.
      }
    }
    if (plan.type !== 'sweep_plan' || !Array.isArray(plan.segments) || !plan.segments.length) throw new Error('Worker did not return a valid sweep plan.');
    // The first/last FFT bin may sit almost one bin inside the requested edge.
    // Give edge cells that width; never manufacture unmeasured endpoint values.
    cfg.pointCount = Math.min(cfg.pointCount, Math.max(2, Math.floor((cfg.stopHz - cfg.startHz) / (2 * 112e6 / n))));
    const callback = this.callback;
    await this.stopSweep(); this.config = cfg;
    if (callback) await this.startSweep(callback);
    return { startHz: cfg.startHz, stopHz: cfg.stopHz, pointCount: cfg.pointCount, controls: cfg.controls,
      ...(automatic ? {} : { rbwHz: cfg.rbwHz }), resolved: { rbwHz: cfg.rbwHz, sweepTimeMs: Math.max(30000, plan.segments.length * 1500) } };
  }
  arguments(cfg) {
    return ['--serial', this.serial, '--start-hz', String(cfg.startHz), '--stop-hz', String(cfg.stopHz), '--rbw-hz', String(cfg.rbwHz),
      '--attenuation-db', String(cfg.controls.attenuationDb), '--if-gain-db', String(cfg.controls.ifGainDb), '--level-unit', 'dbm'];
  }
  async startSweep(onTrace) {
    if (this.child) return;
    if (!this.opened || !this.config) throw new Error('Configure the analyzer before starting.');
    this.callback = onTrace;
    const assembler = new SweepAssembler(this.config, this.serial);
    this.onWarnings?.(warnings('Sequential acquisition bursts leave RF gaps. Sweep time is measured after each completed pass.'));
    this.child = this.io.stream(['sweep', ...this.arguments(this.config), '--count', '1000000', '--interval-ms', '0'], record => {
      if (record.type === 'stopped' || record.type === 'progress') return;
      const result = assembler.accept(record);
      if (!result) return;
      if (result.skipped) {
        this.onWarnings?.([...warnings('Sequential observations.'), { id: 'missing-sweep', severity: 'warning', message: result.reason }]); return;
      }
      this.lastMeasurement = result.measurement;
      this.onWarnings?.(warnings(`Sequential sweep ${result.measurement.durationSeconds.toFixed(3)} s. RF coverage between captures is discontinuous.`));
      onTrace(result.values);
    }, error => { this.callback = null; this.onFatal?.(error); });
  }
  async stopSweep() {
    this.callback = null;
    const child = this.child;
    if (child) { try { await child.stop(); } finally { if (this.child === child) this.child = null; } }
  }
  async close() {
    try { await this.stopSweep(); } catch { /* Teardown must be idempotent; the supervisor has killed the child. */ }
    finally { this.opened = false; active.delete(this.serial); this.lastMeasurement = null; }
  }
}
export function createSpectrumAnalyzerAdapter(device) { return new RSAAdapter(device); }
export function createDiscovery(io = { query }, now = Date.now) {
  let cache = [], retryAt = -Infinity, inFlight, lastError;
  const discover = async () => {
    if (active.size) return [...active.values()];
    if (opening) return cache;
    if (now() < retryAt) { if (lastError) throw lastError; return cache; }
    lastError = null;
    retryAt = now() + 10000;
    requireCalibration(await io.query(['capabilities']));
    const listing = await io.query(['discover']);
    if (listing.type !== 'discovery' || listing.devices?.length !== 1) { cache = []; return cache; }
    const id = await io.query(['probe', '--level-unit', 'dbm']);
    if (id.model !== 'RSA306B' || !id.serial || id.calibrated !== true) throw new Error('Unsupported RSA306B or unavailable factory correction.');
    cache = [deviceDescriptor(id.serial)];
    return cache;
  };
  return () => {
    if (!inFlight) {
      const task = discover().catch(error => { lastError = error; throw error; }).finally(() => { inFlight = null; if (discoveryTask === task) discoveryTask = null; });
      inFlight = task; discoveryTask = task;
    }
    return inFlight;
  };
}
export const discoverDevices = createDiscovery();
