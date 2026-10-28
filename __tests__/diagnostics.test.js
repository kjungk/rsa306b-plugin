import test from 'node:test';
import assert from 'node:assert/strict';
import { cpSync, copyFileSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { init, configUpdated, destroy } from '../adapter.js';
import { queryProcess } from '../driver/process.js';
const pause=ms=>new Promise(r=>setTimeout(r,ms));
const until=async fn=>{for(let i=0;i<100;i++){if(fn())return;await pause(10);}throw new Error('Diagnostic condition timed out');};
function host(){return {statuses:[],warnings:[],logs:[],updateStatus(status,message){this.statuses.push({status,message});},updateWarnings(warnings){this.warnings=warnings;},log(level,message){this.logs.push({level,message});}};}
test.after(async()=>{await destroy();});
test('a missing bundled runtime reports needs-setup and a successful recheck clears it',async()=>{
 const fixture=mkdtempSync(join(tmpdir(),'rsa306b-diagnostics-'));
 let adapter;
 try {
  copyFileSync(new URL('../adapter.js',import.meta.url),join(fixture,'adapter.js'));
  cpSync(new URL('../driver',import.meta.url),join(fixture,'driver'),{recursive:true});
  writeFileSync(join(fixture,'package.json'),'{"type":"module"}');
  adapter=await import(pathToFileURL(join(fixture,'adapter.js')).href);
  process.env.SB_RSA306B_BACKEND='darwin-arm64';process.env.SB_RSA306B_NATIVE_MOCK='1';delete process.env.SB_RSA306B_MOCK;
  const h=host();adapter.init(h,{});
  await until(()=>h.statuses.at(-1)?.status==='needs-setup');assert.match(h.statuses.at(-1).message,/Reinstall/);
  await adapter.discoverDevices({});await adapter.discoverDevices({});
  assert.equal(h.logs.length,1);assert.ok(h.warnings.some(w=>w.id==='discovery'));
  delete process.env.SB_RSA306B_NATIVE_MOCK;process.env.SB_RSA306B_MOCK='1';adapter.configUpdated(h,{});
  await until(()=>h.statuses.at(-1)?.status==='ok');assert.ok(h.warnings.some(w=>w.id==='simulation'));
  assert.ok(!h.warnings.some(w=>w.id==='discovery'));
 } finally {await adapter?.destroy();rmSync(fixture,{recursive:true,force:true});}
});
test('invalid new configuration cancels an older diagnostic before it can overwrite bad-config',async()=>{
 process.env.SB_RSA306B_MOCK='1';delete process.env.SB_RSA306B_NATIVE_MOCK;
 const h=host();init(h,{});
 delete process.env.SB_RSA306B_MOCK;process.env.SB_RSA306B_BACKEND='win32-x64';
 assert.throws(()=>configUpdated(h,{windowsBackend:'invalid'}),/backend/);
 h.updateStatus('bad-config');await pause(120);assert.equal(h.statuses.at(-1).status,'bad-config');await destroy();
});
test('cancelling a diagnostic query terminates only its owned subprocess',async()=>{
 const controller=new AbortController();
 const query=queryProcess(process.execPath,['-e','setInterval(()=>{},1000)'],{signal:controller.signal});
 controller.abort();await assert.rejects(query,/stopped/);
 assert.deepEqual(await queryProcess(process.execPath,['-e','console.log(JSON.stringify({ok:true}))']),{ok:true});
});
