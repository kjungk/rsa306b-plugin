import { fileURLToPath } from 'node:url';
import { queryProcess, streamProcess } from '../process.js';

export const workerPath = fileURLToPath(new URL('../../runtime/darwin-arm64/rsa306', import.meta.url));
export const calibrationMessage = 'The native worker must support the HW7 factory calibration model for this plugin.';
export function requireCalibration(caps) {
  if (caps?.type !== 'capabilities' || caps.schema_version !== 1 || caps.calibrated !== true ||
      !caps.level_units?.includes('dBm') || !caps.spectrum_schema_versions?.includes(2) ||
      caps.calibration_basis !== 'factory_tables' || caps.algorithm !== 'hw7-fullrange-v1' || caps.absolute_accuracy_verified !== false ||
      caps.calibrated_min_hz !== 9000 || caps.calibrated_max_hz !== 6.2e9 ||
      !Number.isFinite(caps.min_rbw_hz) || caps.min_rbw_hz <= 0) throw new Error(calibrationMessage);
  return caps;
}
export function createIO(config = {}, { portableMock = false, nativeMock = false } = {}) {
  const executable = portableMock ? process.execPath : config.nativeWorkerPath || workerPath;
  const prefix = portableMock ? [fileURLToPath(new URL('../mock/worker.js', import.meta.url))] : [];
  const command = args => [...prefix, ...args, ...(nativeMock ? ['--mock'] : [])];
  return {
    query: (args, options) => queryProcess(executable, command(args), options),
    stream: (args, onRecord, onError) => streamProcess(executable, command([...args, '--stdin-control']), onRecord, onError),
  };
}
export const query = args => createIO().query(args);
export const stream = (args, onRecord, onError) => createIO().stream(args, onRecord, onError);
