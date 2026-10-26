import * as native from './driver/native/adapter.js';
import * as windows from './driver/windows/adapter.js';
import { createIO as nativeIO, requireCalibration } from './driver/native/worker.js';
import { createIO as windowsIO, requireApi } from './driver/windows/worker.js';
import { shutdownWorkers } from './driver/process.js';
import path from 'node:path';

export const PRODUCT = 'plugin:rsa306b/rsa306b';
const backends = new Map();
function backend(config = {}) {
  const realMock = process.env.SB_RSA306B_NATIVE_MOCK === '1';
  const portableMock = process.env.SB_RSA306B_MOCK === '1' && !realMock;
  const target = portableMock ? 'darwin-arm64' : process.env.SB_RSA306B_BACKEND || `${process.platform}-${process.arch}`;
  for (const field of target === 'win32-x64' ? ['pythonPath', 'windowsApiPath'] : ['nativeWorkerPath']) {
    const value = config[field];
    if (value === undefined || value === null || value === '') continue;
    const absolute = field === 'windowsApiPath' ? path.win32.isAbsolute : path.isAbsolute;
    if (typeof value !== 'string' || !absolute(value)) throw new Error(`${field} must be an absolute path.`);
  }
  const key = JSON.stringify([target, portableMock, realMock, config.nativeWorkerPath, config.pythonPath, config.windowsApiPath]);
  if (backends.has(key)) return backends.get(key);
  if (!['darwin-arm64', 'win32-x64'].includes(target)) throw new Error('RSA306B supports macOS Apple Silicon and Windows x64 only.');
  const module = target === 'win32-x64' ? windows : native;
  const io = target === 'win32-x64' ? windowsIO(config, { nativeMock: realMock }) : nativeIO(config, { portableMock, nativeMock: realMock });
  const check = target === 'win32-x64' ? requireApi : requireCalibration;
  const result = { target, module, io, discover: module.createDiscovery(io), diagnose: async signal => check(await io.query(['capabilities'], { signal })) };
  backends.set(key, result); return result;
}

export async function discoverDevices(pluginConfig = {}) {
  const current = generation;
  try {
    const devices = await backend(pluginConfig).discover();
    if (current === generation && discoveryFailure) { discoveryFailure = ''; publishWarnings(); }
    return devices;
  } catch (error) {
    const detail = String(error.message).slice(0, 1500);
    if (current === generation && detail !== discoveryFailure) {
      discoveryFailure = detail; diagnosticHost?.log('warn', `RSA306B discovery: ${detail}`); publishWarnings();
    }
    return [];
  }
}
export function createSpectrumAnalyzerAdapter(device, pluginConfig = {}) {
  const selected = backend(pluginConfig);
  return new selected.module.RSAAdapter(device, selected.io);
}

let generation = 0, retryTimer, diagnosticHost, diagnosticAbort, discoveryFailure = '', simulation = false;
function publishWarnings() {
  diagnosticHost?.updateWarnings([
    ...(simulation ? [{ id: 'simulation', severity: 'warning', message: 'Simulation is enabled. These traces are synthetic and must not be used for RF coordination.' }] : []),
    ...(discoveryFailure ? [{ id: 'discovery', severity: 'warning', message: discoveryFailure }] : []),
  ]);
}
export function init(plugin, config = {}) { startDiagnostics(plugin, config); }
export function configUpdated(plugin, config = {}) { startDiagnostics(plugin, config); }
function startDiagnostics(plugin, config) {
  const current = ++generation;
  clearTimeout(retryTimer);
  diagnosticAbort?.abort(); diagnosticAbort = new AbortController();
  const signal = diagnosticAbort.signal;
  const selected = backend(config); // Validation failure cannot let an older check overwrite bad-config.
  diagnosticHost = plugin; discoveryFailure = '';
  simulation = process.env.SB_RSA306B_MOCK === '1' || process.env.SB_RSA306B_NATIVE_MOCK === '1';
  publishWarnings();
  plugin.updateStatus('connecting', 'Checking the RSA306B acquisition runtime.');
  const check = async () => {
    try {
      await selected.diagnose(signal);
      if (current === generation) plugin.updateStatus('ok');
    } catch (error) {
      if (current === generation) {
        const action = selected.target === 'win32-x64'
          ? 'Install the official Tektronix x64 API/USB driver and configure Windows API path. If Python is missing, reinstall the plugin package.'
          : 'Reinstall the complete macOS arm64 plugin package, or build its native worker for a development checkout.';
        plugin.updateStatus('needs-setup', `${action} ${String(error.message).slice(0, 1200)}`);
        retryTimer = setTimeout(check, 30000); retryTimer.unref?.();
      }
    }
  };
  void check(); // The shell handshake/configuration response never waits for a native call.
}
export function destroy() {
  generation++; clearTimeout(retryTimer); diagnosticAbort?.abort(); diagnosticHost = null; backends.clear(); return shutdownWorkers();
}
