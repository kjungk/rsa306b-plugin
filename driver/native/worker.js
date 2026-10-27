import { fileURLToPath } from 'node:url';
import { queryProcess, streamProcess } from '../process.js';

const bundledWorker = target => fileURLToPath(new URL(target === 'win32-x64'
  ? '../../runtime/win32-x64/rsa306.exe' : '../../runtime/darwin-arm64/rsa306', import.meta.url));
export const workerPath = bundledWorker(`${process.platform}-${process.arch}`);
export const calibrationMessage = 'The native worker must support the HW7 factory calibration model for this plugin.';
export function requireCalibration(caps) {
  if (caps?.type !== 'capabilities' || caps.schema_version !== 1 || caps.calibrated !== true ||
      !caps.level_units?.includes('dBm') || !caps.spectrum_schema_versions?.includes(2) ||
      caps.calibration_basis !== 'factory_tables' || caps.algorithm !== 'hw7-fullrange-v1' || caps.absolute_accuracy_verified !== false ||
      caps.calibrated_min_hz !== 9000 || caps.calibrated_max_hz !== 6.2e9 ||
      !Number.isFinite(caps.min_rbw_hz) || caps.min_rbw_hz <= 0) throw new Error(calibrationMessage);
  return caps;
}
export function createIO(config = {}, { target = `${process.platform}-${process.arch}`, portableMock = false, nativeMock = false } = {}) {
  const executable = portableMock ? process.execPath : config.nativeWorkerPath || bundledWorker(target);
  const prefix = portableMock ? [fileURLToPath(new URL('../mock/worker.js', import.meta.url))] : [];
  const command = args => [...prefix, ...args, ...(nativeMock ? ['--mock'] : [])];
  // The Cypress driver may need a bounded cancellation drain after a timeout.
  // Give it time to release pending buffers before the supervisor's hard stop.
  const windows = target === 'win32-x64';
  return {
    query: (args, options) => queryProcess(executable, command(args), { ...(windows ? { timeoutMs: 30000 } : {}), ...options }),
    stream: (args, onRecord, onError) => streamProcess(executable, command([...args, '--stdin-control']), onRecord, onError,
      windows ? { stopTimeoutMs: 20000 } : {}),
  };
}
export const query = args => createIO().query(args);
export const stream = (args, onRecord, onError) => createIO().stream(args, onRecord, onError);
