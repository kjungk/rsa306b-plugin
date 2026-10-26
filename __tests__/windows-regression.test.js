import test from 'node:test';
import assert from 'node:assert/strict';
import { RSAAdapter,validateSweep,createDiscovery } from '../driver/windows/adapter.js';
import { requireApi } from '../driver/windows/worker.js';
const caps={type:'capabilities',schema_version:1,backend:'tektronix-windows-api',api_version:'test',min_hz:9000,max_hz:6.2e9,min_rbw_hz:1000,max_rbw_hz:1e6,calibration_basis:'vendor_api',level_unit:'dBm',absolute_accuracy_verified:false};
const cfg={startHz:10000,stopHz:14000,pointCount:3,requestedRBW:1000,refLevelDbm:-20,segmentCount:1};
const record=()=>({type:'sweep',schema_version:1,complete:true,level_unit:'dBm',calibration_basis:'vendor_api',absolute_accuracy_verified:false,serial:'TEST',api_version:'test',sequence:0,start_hz:10000,stop_hz:14000,point_count:3,requested_rbw_hz:1000,reference_dbm:-20,host_begin:10,duration_seconds:1,values:[-80,-70,-60],segments:[{raw_status:0,timestamp_regressed:false,device_timestamp_ticks:'100',host_begin:10,host_end:11,actual_start_hz:10000,actual_step_hz:1000,actual_rbw_hz:1000,reference_dbm:-20,trace_length:5,gap_seconds:null}]});
test('Windows provenance and complete quality-clean trace contract',()=>{
 assert.equal(requireApi(caps),caps);assert.throws(()=>requireApi({...caps,calibration_basis:'factory_tables'}));
 assert.deepEqual(validateSweep(record(),cfg,'TEST').values,[-80,-70,-60]);
 for(const change of [r=>r.serial='OTHER',r=>r.level_unit='dBFS',r=>r.complete=false,r=>r.values[0]=NaN,r=>r.segments[0].raw_status=32,r=>r.segments[0].timestamp_regressed=true,r=>r.segments[0].host_end=9,r=>r.reference_dbm=0]){
  const r=record();change(r);assert.throws(()=>validateSweep(r,cfg,'TEST'));
 }
 assert.throws(()=>validateSweep(record(),cfg,'TEST',0));
});
function fakeIO(){const events=[];return {events,query:async args=>{
 events.push(args[0]);
 if(args[0]==='capabilities')return caps;
 if(args[0]==='discover')return {type:'discovery',device_count:1};
 if(args[0]==='probe')return {type:'device',serial:'TEST',model:'RSA306B',calibration_basis:'vendor_api'};
 if(args[0]==='configure')return {type:'configuration',serial:'TEST',segments:Array(Math.ceil((Number(args[args.indexOf('--stop-hz')+1])-Number(args[args.indexOf('--start-hz')+1]))/20e6)).fill({actualRBW:9999,reference_dbm:Number(args[args.indexOf('--reference-dbm')+1])})};
 throw new Error('unexpected');
},stream:()=>{events.push('start');return {stop:async()=>{events.push('stop');await new Promise(r=>setTimeout(r,3));events.push('exit');}};}};}
test('Windows retune releases the old API process before configuring',async()=>{
 const io=fakeIO(),a=new RSAAdapter({config:{serial:'TEST'}},io);await a.open();
 const cfg=await a.applyConfig({rbwHz:10000});assert.equal(cfg.resolved.rbwHz,9999);
 await a.startSweep(()=>{});await a.applyConfig({stopHz:698e6});await a.close();
 assert.deepEqual(io.events,['capabilities','probe','configure','start','stop','exit','configure','start','stop','exit']);
});
test('Windows settings clamp before API configuration and report actual reference level',async()=>{
 const io=fakeIO(),a=new RSAAdapter({config:{serial:'TEST'}},io);await a.open();
 const result=await a.applyConfig({startHz:0,stopHz:7e9,rbwHz:0,pointCount:1,controls:{refLevelDbm:100}});
 assert.equal(result.startHz,9000);assert.equal(result.stopHz,6.2e9);assert.equal(result.pointCount,2);
 assert.equal(result.controls.refLevelDbm,20);await a.close();
});
test('Windows discovery caches probes briefly and deduplicates concurrent requests',async()=>{
 const io=fakeIO(),discover=createDiscovery(io,()=>20000);
 const [a,b]=await Promise.all([discover(),discover()]);assert.equal(a[0].id,'usb:rsa306b:TEST');assert.deepEqual(a,b);
 await discover();assert.equal(io.events.filter(x=>x==='probe').length,1);
});

test('reference level device control survives later settings patches',async()=>{
 const io=fakeIO(),a=new RSAAdapter({config:{serial:'TEST'}},io);await a.open();
 const changed=await a.applyConfig({controls:{refLevelDbm:-10}});assert.equal(changed.controls.refLevelDbm,-10);
 const retuned=await a.applyConfig({stopHz:698e6});assert.equal(retuned.controls.refLevelDbm,-10);
 await a.close();
});

test('discovery retains the analyzer while a slow API connection is opening',async()=>{
 const io=fakeIO();let now=20000;const discover=createDiscovery(io,()=>now);
 const cached=await discover();const original=io.query;let release;let entered;
 const waiting=new Promise(r=>{entered=r;});let delayed=true;
 io.query=async args=>{if(args[0]==='probe'&&delayed){delayed=false;entered();await new Promise(r=>release=r);}return original(args);};
 const adapter=new RSAAdapter(cached[0],io);const open=adapter.open();await waiting;
 now=40000;const before=io.events.length;
 assert.deepEqual(await discover(),cached);assert.equal(io.events.length,before);
 release();await open;await adapter.close();
});

test('Windows settings reject malformed numbers, ignore native controls, and require explicit matching serial',async()=>{
 const io=fakeIO();await assert.rejects(new RSAAdapter({},io).open(),/serial/);
 await assert.rejects(new RSAAdapter({id:'usb:rsa306b:TEST',config:{serial:'OTHER'}},io).open(),/serial/);
 const a=new RSAAdapter({config:{serial:'TEST'}},io);await a.open();
 try {
  for(const request of [{pointCount:NaN},{rbwHz:Infinity},{controls:{refLevelDbm:'loud'}}])await assert.rejects(a.applyConfig(request),/finite/);
  const c=await a.applyConfig({controls:{attenuationDb:12,ifGainDb:10}});assert.deepEqual(c.controls,{refLevelDbm:-20});
 }finally{await a.close();}
});
