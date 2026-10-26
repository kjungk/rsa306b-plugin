import { query, stream, requireApi } from './worker.js';
import { clamp, finiteOr, normalizeRange, selectedSerial, deviceDescriptor, validateRequest } from '../settings.js';
export const PRODUCT='plugin:rsa306b/rsa306b';
const active=new Map();
let opening = 0, discoveryTask;
const warnings=message=>[
 {id:'vendor-api',severity:'warning',message:'Tektronix API dBm. Absolute RF accuracy has not been independently verified.'},
 {id:'sequential-captures',severity:'info',message}];

export function validateSweep(r,cfg,serial,lastSequence=-1) {
  if(r?.type!=='sweep'||r.schema_version!==1||r.complete!==true||r.level_unit!=='dBm'||
     r.calibration_basis!=='vendor_api'||r.absolute_accuracy_verified!==false||r.serial!==serial||
     !r.api_version||!Number.isSafeInteger(r.sequence)||r.sequence<=lastSequence||
     r.start_hz!==cfg.startHz||r.stop_hz!==cfg.stopHz||r.point_count!==cfg.pointCount||
     r.requested_rbw_hz!==cfg.requestedRBW||r.reference_dbm!==cfg.refLevelDbm||
     !Array.isArray(r.values)||r.values.length!==cfg.pointCount||!r.values.every(Number.isFinite)||
     !Number.isFinite(r.host_begin)||!Number.isFinite(r.duration_seconds)||r.duration_seconds<=0||
     !Array.isArray(r.segments)||r.segments.length!==cfg.segmentCount)throw new Error('Invalid Windows API sweep; trace withheld.');
  let previousEnd=-Infinity,previousTicks=null;
  for(const s of r.segments) {
    if(s.raw_status!==0||s.timestamp_regressed!==false||!/^\d+$/.test(s.device_timestamp_ticks)||
       ![s.host_begin,s.host_end,s.actual_start_hz,s.actual_step_hz,s.actual_rbw_hz,s.reference_dbm].every(Number.isFinite)||
       s.host_end<s.host_begin||s.host_begin<previousEnd||s.actual_step_hz<=0||s.actual_rbw_hz<=0||
       Math.abs(s.reference_dbm-cfg.refLevelDbm)>1e-6||!Number.isInteger(s.trace_length)||s.trace_length<2||s.trace_length>64001||
       (s.gap_seconds!==null&&(!Number.isFinite(s.gap_seconds)||s.gap_seconds<0)))throw new Error('Invalid API quality, timing or settings; trace withheld.');
    const ticks=BigInt(s.device_timestamp_ticks);
    if(previousTicks!==null&&ticks<=previousTicks)throw new Error('API timestamps regressed.');
    previousEnd=s.host_end;previousTicks=ticks;
  }
  return {values:r.values,measurement:{begin:r.host_begin,durationSeconds:r.duration_seconds,
    calibrationBasis:'vendor_api',apiVersion:r.api_version,absoluteAccuracyVerified:false,segmentTimes:r.segments}};
}

export class RSAAdapter {
  constructor(device = {}, io = { query, stream }) {
    this.device = device; this.io = io; this.child = null; this.opened = false; this.config = null; this.lastMeasurement = null;
  }
  async open() {
    if (discoveryTask) await discoveryTask.catch(() => {});
    if (this.opened) return this.openInfo;
    if (this.opening || opening || active.size) throw new Error('The RSA306B is already open in this plugin. Close its other live scan first.');
    const serial = selectedSerial(this.device);
    this.opening = true; opening++;
    try {
      this.caps = requireApi(await this.io.query(['capabilities']));
      const id = await this.io.query(['probe', '--serial', serial]);
      if (id.type !== 'device' || id.model !== 'RSA306B' || id.serial !== serial || id.calibration_basis !== 'vendor_api') {
        throw new Error('Connected analyzer differs from the selected RSA306B.');
      }
      this.serial = id.serial; this.opened = true; active.set(this.serial, deviceDescriptor(this.serial));
      this.openInfo = { identity: { model: id.model, serial: id.serial, firmware: id.firmware, name: `RSA306B ${id.serial}` },
        capabilities: { minFrequencyHz: this.caps.min_hz, maxFrequencyHz: this.caps.max_hz, minPointCount: 2, maxPointCount: 2000, defaultPointCount: 1001,
          rbwHz: [1000,3000,10000,30000,100000,300000,1000000],
          controls: [{ id: 'refLevelDbm', type: 'number', label: 'Reference level', unit: 'dBm', min: -50, max: 20, step: 1, default: -20 }] } };
      return this.openInfo;
    } finally { this.opening = false; opening--; }
  }
  arguments(c) {
    return ['--serial', this.serial, '--start-hz', String(c.startHz), '--stop-hz', String(c.stopHz),
      '--rbw-hz', String(c.requestedRBW), '--reference-dbm', String(c.refLevelDbm), '--points', String(c.pointCount)];
  }
  async applyConfig(request = {}) {
    if (!this.opened) throw new Error('Open the analyzer first.');
    validateRequest(request, ['refLevelDbm']);
    const prior = this.config ?? { startHz: 470e6, stopHz: 694e6, pointCount: 1001, refLevelDbm: -20 };
    const c = { ...normalizeRange(request, prior, this.caps.min_hz, this.caps.max_hz, 3000),
      pointCount: clamp(Math.round(finiteOr(request.pointCount, prior.pointCount)), 2, 2000),
      refLevelDbm: clamp(Math.round(finiteOr(request.controls?.refLevelDbm, prior.refLevelDbm)), -50, 20) };
    const automatic = !Number.isFinite(request.rbwHz);
    c.requestedRBW = clamp(automatic ? Math.min(10000, (c.stopHz-c.startHz)/1000) : request.rbwHz,
      this.caps.min_rbw_hz, Math.min(this.caps.max_rbw_hz, (c.stopHz-c.startHz)/3));
    const callback = this.callback;
    await this.stopSweep();
    const actual = await this.io.query(['configure', ...this.arguments(c)]);
    if (actual.type !== 'configuration' || actual.serial !== this.serial || !Array.isArray(actual.segments) ||
        actual.segments.length !== Math.ceil((c.stopHz-c.startHz)/20e6) ||
        actual.segments.some(s => ![s.actualRBW,s.reference_dbm].every(Number.isFinite) || s.actualRBW <= 0 || s.reference_dbm < -50 || s.reference_dbm > 20)) {
      throw new Error('Tektronix API did not confirm a valid configuration.');
    }
    c.refLevelDbm = actual.segments[0].reference_dbm;
    if (actual.segments.some(s => Math.abs(s.reference_dbm-c.refLevelDbm) > 1e-6)) throw new Error('Reference level changed between sweep segments.');
    c.segmentCount = actual.segments.length; c.rbwHz = Math.max(...actual.segments.map(s => s.actualRBW));
    c.controls = { refLevelDbm: c.refLevelDbm }; this.config = c;
    if (callback) await this.startSweep(callback);
    return { startHz: c.startHz, stopHz: c.stopHz, pointCount: c.pointCount, controls: c.controls,
      ...(automatic ? {} : { rbwHz: c.rbwHz }), resolved: { rbwHz: c.rbwHz, sweepTimeMs: Math.max(30000, c.segmentCount*1000) } };
  }
  async startSweep(onTrace) {
    if (this.child) return;
    if (!this.opened || !this.config) throw new Error('Configure the analyzer before scanning.');
    this.callback = onTrace; let lastSequence = -1;
    this.onWarnings?.(warnings('Sequential API acquisitions leave gaps. Duration is measured after a complete pass.'));
    this.child = this.io.stream(['sweep', ...this.arguments(this.config)], r => {
      if (r.type === 'device') { if (r.serial !== this.serial || r.model !== 'RSA306B') throw new Error('Analyzer changed.'); return; }
      if (r.type === 'progress' || r.type === 'stopped') return;
      if (r.type === 'gap') { this.onWarnings?.([...warnings('Sequential API observations.'), { id: 'missing-sweep', severity: 'warning', message: 'Incomplete or flagged API sweep withheld; displayed trace is older.' }]); return; }
      const result = validateSweep(r, this.config, this.serial, lastSequence); lastSequence = r.sequence;
      this.lastMeasurement = result.measurement;
      this.onWarnings?.(warnings(`Sequential sweep ${result.measurement.durationSeconds.toFixed(3)} s; discontinuous RF coverage.`));
      onTrace(result.values);
    }, error => { this.callback = null; this.onFatal?.(error); });
  }
  async stopSweep() {
    this.callback = null; const child = this.child;
    if (child) { try { await child.stop(); } finally { if (this.child === child) this.child = null; } }
  }
  async close() {
    try { await this.stopSweep(); } catch { /* Teardown is idempotent; supervisor has killed the child. */ }
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
    requireApi(await io.query(['capabilities']));
    const found = await io.query(['discover']);
    if (found.type !== 'discovery' || found.device_count !== 1) { cache = []; return cache; }
    const id = await io.query(['probe']);
    if (id.type !== 'device' || id.model !== 'RSA306B' || !id.serial || id.calibration_basis !== 'vendor_api') throw new Error('Unsupported Windows analyzer.');
    cache = [deviceDescriptor(id.serial)]; return cache;
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
