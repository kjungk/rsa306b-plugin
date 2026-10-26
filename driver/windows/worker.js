import { fileURLToPath } from 'node:url';
import { queryProcess, streamProcess } from '../process.js';

export const runtime = fileURLToPath(new URL('../../runtime/win32-x64/python.exe', import.meta.url));
const script = fileURLToPath(new URL('./worker.py', import.meta.url));
export function requireApi(caps) {
  if (caps?.type !== 'capabilities' || caps.schema_version !== 1 || caps.backend !== 'tektronix-windows-api' ||
      caps.calibration_basis !== 'vendor_api' || caps.level_unit !== 'dBm' || caps.absolute_accuracy_verified !== false ||
      caps.min_hz !== 9000 || caps.max_hz !== 6.2e9 || caps.min_rbw_hz !== 1000 || caps.max_rbw_hz !== 1e6 || !caps.api_version) {
    throw new Error('Windows Tektronix API capability contract is invalid.');
  }
  return caps;
}
export function createIO(config = {}, { nativeMock = false } = {}) {
  const executable = config.pythonPath || runtime;
  const command = args => ['-I', '-B', '-u', script, ...args, ...(config.windowsApiPath ? ['--dll', config.windowsApiPath] : []), ...(nativeMock ? ['--mock'] : [])];
  return {
    query: (args, options) => queryProcess(executable, command(args), { timeoutMs: 60000, ...options }),
    stream: (args, onRecord, onError) => streamProcess(executable, command([...args, '--stdin-control']), onRecord, onError, { stopTimeoutMs: 12000 }),
  };
}
export const query = args => createIO().query(args);
export const stream = (args, onRecord, onError) => createIO().stream(args, onRecord, onError);
