import test from 'node:test';
import assert from 'node:assert/strict';
import { createSpectrumAnalyzerAdapter, destroy } from '../adapter.js';
import { RSAAdapter as NativeAdapter } from '../driver/native/adapter.js';
import { RSAAdapter as VendorAdapter } from '../driver/windows/adapter.js';

test.afterEach(async () => { delete process.env.SB_RSA306B_BACKEND; await destroy(); });
test('Windows defaults to native gain controls; API fallback is explicit', () => {
  process.env.SB_RSA306B_BACKEND = 'win32-x64';
  assert.ok(createSpectrumAnalyzerAdapter({}) instanceof NativeAdapter);
  assert.ok(createSpectrumAnalyzerAdapter({}, {windowsBackend:'tektronix-api'}) instanceof VendorAdapter);
  assert.ok(createSpectrumAnalyzerAdapter({}, {windowsBackend:'native'}) instanceof NativeAdapter);
  assert.throws(() => createSpectrumAnalyzerAdapter({}, {windowsBackend:'typo'}), /backend/);
  assert.ok(createSpectrumAnalyzerAdapter({}, {nativeWorkerPath:'obsolete.exe',pythonPath:'obsolete-python'}) instanceof NativeAdapter);
  assert.ok(createSpectrumAnalyzerAdapter({}, {windowsBackend:'tektronix-api',pythonPath:'obsolete-python',windowsApiPath:'obsolete.dll'}) instanceof VendorAdapter);
});
test('Mac remains native when a shared project carries Windows fallback settings', () => {
  process.env.SB_RSA306B_BACKEND = 'darwin-arm64';
  assert.ok(createSpectrumAnalyzerAdapter({}, {windowsBackend:'tektronix-api',windowsApiPath:'C:\\Tektronix\\RSA_API.dll'}) instanceof NativeAdapter);
});
