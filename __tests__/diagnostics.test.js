import test from 'node:test';
import assert from 'node:assert/strict';
import { init, configUpdated, destroy, discoverDevices } from '../adapter.js';
import { queryProcess } from '../driver/process.js';
const pause=ms=>new Promise(r=>setTimeout(r,ms));
const until=async fn=>{for(let i=0;i<100;i++){if(fn())return;await pause(10);}throw new Error('Diagnostic condition timed out');};
function host(){return {statuses:[],warnings:[],logs:[],updateStatus(status,message){this.statuses.push({status,message});},updateWarnings(warnings){this.warnings=warnings;},log(level,message){this.logs.push({level,message});}};}
test.after(async()=>{await destroy();});
test('prerequisites report needs-setup and clear after corrected configuration',async()=>{
 process.env.SB_RSA306B_BACKEND='darwin-arm64';process.env.SB_RSA306B_NATIVE_MOCK='1';
 const h=host();init(h,{nativeWorkerPath:'/__rsa306b_missing_worker__'});
 await until(()=>h.statuses.at(-1)?.status==='needs-setup');assert.match(h.statuses.at(-1).message,/Reinstall/);
 await discoverDevices({nativeWorkerPath:'/__rsa306b_missing_worker__'});await discoverDevices({nativeWorkerPath:'/__rsa306b_missing_worker__'});
 assert.equal(h.logs.length,1);assert.ok(h.warnings.some(w=>w.id==='discovery'));
 delete process.env.SB_RSA306B_NATIVE_MOCK;process.env.SB_RSA306B_MOCK='1';configUpdated(h,{});
 await until(()=>h.statuses.at(-1)?.status==='ok');assert.ok(h.warnings.some(w=>w.id==='simulation'));
 assert.ok(!h.warnings.some(w=>w.id==='discovery'));await destroy();
});
test('invalid new configuration cancels an older diagnostic before it can overwrite bad-config',async()=>{
 process.env.SB_RSA306B_MOCK='1';delete process.env.SB_RSA306B_NATIVE_MOCK;
 const h=host();init(h,{});
 assert.throws(()=>configUpdated(h,{nativeWorkerPath:'relative/path'}),/absolute path/);
 h.updateStatus('bad-config');await pause(120);assert.equal(h.statuses.at(-1).status,'bad-config');await destroy();
});
test('cancelling a diagnostic query terminates only its owned subprocess',async()=>{
 const controller=new AbortController();
 const query=queryProcess(process.execPath,['-e','setInterval(()=>{},1000)'],{signal:controller.signal});
 controller.abort();await assert.rejects(query,/stopped/);
 assert.deepEqual(await queryProcess(process.execPath,['-e','console.log(JSON.stringify({ok:true}))']),{ok:true});
});
