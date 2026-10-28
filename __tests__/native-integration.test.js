import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { createIO, workerPath } from '../driver/native/worker.js';
import { RSAAdapter } from '../driver/native/adapter.js';
import { createSpectrumAnalyzerAdapter, destroy } from '../adapter.js';
const candidates = [process.env.SB_RSA306B_NATIVE_TEST_WORKER,
  workerPath,
  fileURLToPath(new URL('../native/build/rsa306',import.meta.url))].filter(Boolean);
const executable = ['darwin-arm64','win32-x64'].includes(`${process.platform}-${process.arch}`) ? candidates.find(existsSync) : undefined;
const skip = executable ? false : 'Native worker absent; real-worker tests MUST run on both platform artifact jobs.';
const io = createIO({nativeWorkerPath:executable},{nativeMock:true});

test('saved runtime overrides do not prevent opening the bundled native worker', {
  skip: !executable || !existsSync(workerPath) ? 'Bundled native runtime required.' : false,
}, async () => {
  process.env.SB_RSA306B_NATIVE_MOCK='1';
  const a=createSpectrumAnalyzerAdapter({config:{serial:'MOCK-RSA306B'}}, {
    nativeWorkerPath:'obsolete-worker',pythonPath:'obsolete-python',
  });
  try {
    const info=await a.open();assert.equal(info.identity.serial,'MOCK-RSA306B');
  } finally {
    await a.close();await destroy();delete process.env.SB_RSA306B_NATIVE_MOCK;
  }
});

test('real native planner clamps minimum RBW across the full 9 kHz–6.2 GHz range', {skip}, async () => {
  const a = new RSAAdapter({config:{serial:'MOCK-RSA306B'}},io);
  await a.open();
  try {
    const effective=await a.applyConfig({startHz:9000,stopHz:6.2e9,rbwHz:160.21728515625,pointCount:2000});
    assert.equal(effective.startHz,9000);assert.equal(effective.stopHz,6.2e9);
    assert.ok(effective.rbwHz>160.21728515625);assert.ok(effective.pointCount<=2000);
  } finally {await a.close();}
});
test('real native FFT supplies every output cell for narrow LF, arbitrary RF and LF/RF seam', {skip,timeout:30000}, async () => {
  for (const [startHz,stopHz] of [[9000,10000],[594000123,594010123],[21e6,23e6]]) {
    const a=new RSAAdapter({config:{serial:'MOCK-RSA306B'}},io);
    await a.open();
    try {
      const effective=await a.applyConfig({startHz,stopHz,pointCount:2000});
      let resolve,reject,timer;
      const trace=new Promise((r,j)=>{resolve=r;reject=j;timer=setTimeout(()=>j(new Error('No complete real-mock trace')),10000);});
      a.onFatal=reject;
      await a.startSweep(resolve);
      let values;try{values=await trace;}finally{clearTimeout(timer);}
      assert.equal(values.length,effective.pointCount);assert.ok(values.every(Number.isFinite));
    } finally {await a.close();}
  }
});
