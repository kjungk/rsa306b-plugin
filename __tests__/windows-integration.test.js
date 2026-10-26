import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { createIO, runtime } from '../driver/windows/worker.js';
import { RSAAdapter, createDiscovery } from '../driver/windows/adapter.js';
const script=fileURLToPath(new URL('../driver/windows/worker.py',import.meta.url));
const python=process.env.SB_RSA306B_TEST_PYTHON || (process.platform==='win32'?(existsSync(runtime)?runtime:'python'):'python3');
const available=spawnSync(python,['--version'],{windowsHide:true}).status===0;
const skip=available?false:'Python absent; real Windows worker mock MUST run in the Windows artifact job.';
const io=createIO({pythonPath:python},{nativeMock:true});
test('real Python mock uses explicit serial, ctypes API configuration and finite complete dBm sweeps',{skip,timeout:20000},async()=>{
  const found=await createDiscovery(io)();assert.equal(found[0].id,'usb:rsa306b:MOCK-RSA306B');
  await assert.rejects(io.query(['probe','--serial','OTHER']),/differs/);
  const a=new RSAAdapter(found[0],io);await a.open();
  try {
    const cfg=await a.applyConfig({startHz:9000,stopHz:23000000,pointCount:401,controls:{refLevelDbm:-10}});
    assert.equal(cfg.controls.refLevelDbm,-10);
    let resolve,reject,timer;const trace=new Promise((r,j)=>{resolve=r;reject=j;timer=setTimeout(()=>j(new Error('No Python mock trace')),10000);});
    a.onFatal=reject;await a.startSweep(resolve);
    let values;try{values=await trace;}finally{clearTimeout(timer);}
    assert.equal(values.length,cfg.pointCount);assert.ok(values.every(Number.isFinite));
    await a.stopSweep();await a.stopSweep();
  }finally{await a.close();}
});
test('Windows API override is explicit and no machine-local location file is read',{skip},()=>{
  const code=`import runpy,tempfile,pathlib;w=runpy.run_path(${JSON.stringify(script)});assert w['Settings'].actualNumIQSamples.offset==88;assert w['C'].sizeof(w['Settings'])==96\ntry:w['locate_api']('relative.dll')\nexcept ValueError:pass\nelse:raise AssertionError('relative path accepted')\nwith tempfile.TemporaryDirectory() as d:\n p=pathlib.Path(d)/'RSA_API.dll';p.write_bytes(b'fixture');assert w['locate_api'](d)[0]==p`;
  const r=spawnSync(python,['-I','-B','-c',code],{encoding:'utf8',windowsHide:true});assert.equal(r.status,0,r.stderr);
});
