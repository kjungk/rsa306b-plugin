#!/usr/bin/env node
// Explicit opt-in acceptance client for an extracted candidate's native backend.
// Never installs the plugin or edits a SoundBase project. Disconnect mode waits
// for the operator to physically unplug/reconnect; it never changes USB drivers.
import { spawn, execFile } from 'node:child_process';
import { parseArgs } from 'node:util';
import { readFileSync, writeFileSync, renameSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { randomBytes, createHash } from 'node:crypto';
import assert from 'node:assert/strict';

const usage = 'Usage: node scripts/check-hardware.mjs EXTRACTED_PLUGIN NEW_REPORT.json --hardware [--mode acquisition|controls|lifecycle|soak|disconnect] [--seconds 1800] [--archive ZIP] [--max-seconds N]';
const {values,positionals} = parseArgs({options:{
  hardware:{type:'boolean'},mode:{type:'string',default:'acquisition'},
  seconds:{type:'string'},archive:{type:'string'},'max-seconds':{type:'string'},
  help:{type:'boolean'},
},strict:true,allowPositionals:true});
if(values.help){console.log(usage);process.exit(0);}
const [folder,reportFile]=positionals,mode=values.mode;
if(positionals.length!==2 || !values.hardware || !['acquisition','controls','lifecycle','soak','disconnect'].includes(mode))throw new Error(usage);
if (!['darwin-arm64','win32-x64'].includes(`${process.platform}-${process.arch}`)) throw new Error('Unsupported acceptance target');
if(values.seconds!==undefined && mode!=='soak')throw new Error('--seconds is only valid for soak mode');
const seconds=Number(values.seconds ?? 1800);
const maxSeconds=Number(values['max-seconds'] ?? (mode==='soak'?seconds+300:mode==='acquisition'?2400:1200));
if(!Number.isFinite(seconds)||seconds<1||seconds>86400||!Number.isFinite(maxSeconds)||maxSeconds<1||maxSeconds>90000)throw new Error('Invalid bounded test duration');
if(mode==='soak' && maxSeconds<=seconds)throw new Error('--max-seconds must exceed the requested soak duration');
const root=resolve(folder),output=resolve(reportFile),archive=values.archive;
const modeGate={acquisition:'acquisition range and retune suite',controls:'native gain/RBW/trace-mode controls',lifecycle:'stop/start and competing-owner lifecycle',disconnect:'physical unplug/reconnect',soak:'30-minute soak'};
const competingAlias='acceptance-competing-owner';
const competingMessage='The RSA306B is already open in this plugin. Close its other live scan first.';
const pkg = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
writeFileSync(output, '', { flag: 'wx' });
const report = {
  schema_version: 1, status: 'partial', automated_status: 'running', mode, backend: 'native',
  target: `${process.platform}-${process.arch}`, node_version: process.version,
  plugin_version: pkg.version, content_hash: pkg.soundbase?.contentHash,
  source_commit: pkg.soundbase?.commit, source_dirty: pkg.soundbase?.dirty,
  artifact_sha256: archive ? createHash('sha256').update(readFileSync(resolve(archive))).digest('hex') : null,
  artifact_identity_note: archive
    ? 'Supplied ZIP digest only. Verify that this extracted folder matches the archive with verify-release.py before testing.'
    : 'No ZIP digest supplied; this report alone cannot identify an immutable release candidate.',
  started_at: new Date().toISOString(), max_duration_seconds: maxSeconds,
  stages: [], warning_events: {}, failed_device_events: [],
  pending: [...Object.values(modeGate),'Desktop plot/waterfall and project reload check','normal Desktop exit and enable/disable check','targeted final-package RF confirmation (existing SMHU evidence separately replayed)','controlled hard-host-crash recovery'],
};
function classifyFailedEvents() {
  const rejected=mode==='lifecycle'&&report.stages.some(stage=>stage.name==='competing-owner-rejected'&&stage.status==='passed'&&
    stage.response?.status===503&&stage.response?.body?.detail===competingMessage);
  for(const event of report.failed_device_events) {
    event.expected=Boolean(rejected&&event.id===competingAlias&&event.status?.message===competingMessage);
    event.classification=event.expected?'expected-competing-owner-rejection':mode==='disconnect'?'manual-disconnect-review-required':'unexpected-device-failure';
  }
  return report.failed_device_events.filter(event=>!event.expected);
}
const persist = () => { classifyFailedEvents();const temp = `${output}.tmp`; writeFileSync(temp, `${JSON.stringify(report,(key,value)=>key==='previous_amplitudes'?undefined:value,2)}\n`); renameSync(temp,output); };
persist();
const token = randomBytes(24).toString('hex');
const env = {...process.env, SB_PLUGIN_TOKEN: token};
for (const key of ['SB_RSA306B_MOCK','SB_RSA306B_NATIVE_MOCK','SB_RSA306B_BACKEND','SB_PLUGIN_LIFELINE']) delete env[key];
const child = spawn(process.execPath,[join(root,'main.js')],{cwd:root,env,windowsHide:true,stdio:['ignore','pipe','pipe']});
report.plugin_pid = child.pid;
let logs='', stderr='', exited=false, port, devicePath, aliasPath, interrupted;
const aborted = new AbortController(), eventsAbort = new AbortController();
const done = new Promise(resolveDone => child.once('close',(code,signal) => {exited=true;report.plugin_exit={code,signal};resolveDone();}));
child.stderr.on('data',chunk => {stderr=(stderr+chunk).slice(-32000);});
const interrupt = signal => {interrupted=`Acceptance client received ${signal}`;aborted.abort(new Error(interrupted));};
process.once('SIGINT',()=>interrupt('SIGINT')); process.once('SIGTERM',()=>interrupt('SIGTERM'));
const deadline = setTimeout(() => aborted.abort(new Error(`Test exceeded ${maxSeconds}s overall deadline`)),maxSeconds*1000); deadline.unref();
async function api(path,method='GET',body,{cleanup=false,allowError=false,timeoutMs=60000}={}) {
  const response=await fetch(`http://127.0.0.1:${port}${path}`,{
    method,headers:{authorization:`Bearer ${token}`,'content-type':'application/json'},
    body:body === undefined ? undefined : JSON.stringify(body),
    signal: cleanup ? AbortSignal.timeout(timeoutMs) : AbortSignal.any([aborted.signal,AbortSignal.timeout(timeoutMs)]),
  });
  const text=await response.text(); let data;
  try {data=text ? JSON.parse(text) : null;} catch {throw new Error(`Non-JSON HTTP response ${method} ${path}: ${text.slice(0,500)}`);}
  if(!response.ok && !allowError) { const error=new Error(`${method} ${path}: ${response.status} ${text.slice(0,1200)}`);error.status=response.status;throw error; }
  return allowError ? {status:response.status,body:data} : data;
}
async function collectEvents() {
  try {
    const response=await fetch(`http://127.0.0.1:${port}/events`,{headers:{authorization:`Bearer ${token}`},signal:eventsAbort.signal});
    if(!response.ok) throw new Error(`SSE returned ${response.status}`);
    const reader=response.body.getReader(), decoder=new TextDecoder(); let buffer='';
    for(;;) {
      const {done,value}=await reader.read();if(done) {
        if(!eventsAbort.signal.aborted)report.events_error='Status event stream ended before acceptance completed';
        break;
      }
      buffer+=decoder.decode(value,{stream:true});
      let boundary;
      while((boundary=buffer.indexOf('\n\n'))>=0) {
        const block=buffer.slice(0,boundary);buffer=buffer.slice(boundary+2);
        const data=block.split('\n').filter(line=>line.startsWith('data:')).map(line=>line.slice(5).trimStart()).join('\n');
        if(!data) continue;
        let event;try{event=JSON.parse(data);}catch{continue;}
        const status=event.status || event.device?.status;
        for(const warning of status?.warnings ?? []) {
          // This counts warning events, not dropped sweeps (SDK deduplicates).
          const key=warning.id || warning.message;
          report.warning_events[key] ??= {count:0,first_at:new Date().toISOString(),last_message:warning.message};
          report.warning_events[key].count++;report.warning_events[key].last_message=warning.message;
        }
        if(status?.status==='failed') report.failed_device_events.push({at:new Date().toISOString(),id:event.deviceId || event.device?.id,status});
      }
    }
  } catch(error) {if(!eventsAbort.signal.aborted)report.events_error=error.message;}
}
function validateTrace(trace,effective,lastId) {
  assert.ok(Number.isSafeInteger(trace.sweepId) && trace.sweepId>lastId,'Fresh increasing sweep ID required');
  assert.equal(trace.startHz,effective.startHz);assert.equal(trace.stopHz,effective.stopHz);
  assert.equal(trace.pointCount,effective.pointCount);assert.equal(trace.stepHz,(effective.stopHz-effective.startHz)/(effective.pointCount-1));
  assert.equal(trace.unit,'dBm');assert.equal(trace.amplitudesDbm.length,effective.pointCount);
  assert.ok(trace.amplitudesDbm.every(Number.isFinite),'All trace values must be finite');
  assert.ok(Number.isFinite(Date.parse(trace.timestamp)),'Trace needs a timestamp');
}
let eventTask, currentStage;
async function stage(name,config,count=3,checkEffective=()=>{},durationSeconds=0) {
  const begin=Date.now();
  currentStage={name,status:'running',requested:config,started_at:new Date().toISOString(),traces:[],trace_count:0,stale_responses:0,empty_responses:0};
  report.stages.push(currentStage);persist();
  const effective=await api(devicePath+'/configuration','POST',config);
  currentStage.effective=effective;checkEffective(effective);
  assert.deepEqual(await api(devicePath+'/configuration'),effective,'GET readback differs from POST echo');
  const started=await api(devicePath+'/sweep/start','POST');assert.equal(started.sweeping,true);
  // Never accept the shell's cached trace from before the applied configuration.
  assert.ok(Number.isSafeInteger(started.sweepId),'Start response needs a sweep-ID baseline');
  let lastId=started.sweepId,lastFreshAt=Date.now(),lastPersist=0,lastProgress=0;
  const perTraceMs=Math.min(720000,Math.max(90000,(effective.resolved?.sweepTimeMs || 30000)*1.5+30000));
  currentStage.per_trace_timeout_ms=perTraceMs;currentStage.baseline_sweep_id=lastId;persist();
  const captureStart=Date.now();
  while(currentStage.trace_count<count || (durationSeconds && Date.now()-captureStart<durationSeconds*1000)) {
    if(aborted.signal.aborted) throw aborted.signal.reason;
    if(Date.now()-lastFreshAt>perTraceMs) throw new Error(`${name}: no fresh trace for ${perTraceMs/1000}s`);
    let trace;
    try {trace=await api(devicePath+`/trace?sinceSweepId=${lastId}`);}
    catch(error) {if(error.status!==409) throw error;currentStage.empty_responses++;await new Promise(resolveWait=>setTimeout(resolveWait,50));continue;}
    if(trace.sweepId<=lastId) {currentStage.stale_responses++;await new Promise(resolveWait=>setTimeout(resolveWait,50));continue;}
    validateTrace(trace,effective,lastId);
    if(currentStage.previous_amplitudes && effective.traceMode==='max-hold') assert.ok(trace.amplitudesDbm.every((value,i)=>value>=currentStage.previous_amplitudes[i]),'Max-hold decreased');
    if(currentStage.previous_amplitudes && effective.traceMode==='min-hold') assert.ok(trace.amplitudesDbm.every((value,i)=>value<=currentStage.previous_amplitudes[i]),'Min-hold increased');
    // Kept only in memory and removed before report serialization.
    currentStage.previous_amplitudes=trace.amplitudesDbm;
    const sample={sweep_id:trace.sweepId,timestamp:trace.timestamp,minimum_dbm:Math.min(...trace.amplitudesDbm),maximum_dbm:Math.max(...trace.amplitudesDbm),interval_ms:Date.now()-lastFreshAt};
    lastId=trace.sweepId;lastFreshAt=Date.now();currentStage.trace_count++;
    if(currentStage.traces.length<20)currentStage.traces.push(sample);
    currentStage.last_trace=sample;
    currentStage.elapsed_ms=Date.now()-captureStart;
    currentStage.maximum_trace_interval_ms=Math.max(currentStage.maximum_trace_interval_ms||0,sample.interval_ms);
    if(Date.now()-lastPersist>5000) {persist();lastPersist=Date.now();}
    if(Date.now()-lastProgress>30000 && durationSeconds) {console.log(`${name}: ${Math.round(currentStage.elapsed_ms/1000)}s, ${currentStage.trace_count} traces`);lastProgress=Date.now();}
  }
  delete currentStage.previous_amplitudes;
  currentStage.status='passed';currentStage.duration_ms=Date.now()-begin;persist();
  console.log(`passed: ${name} (${currentStage.trace_count} fresh traces, ${(currentStage.duration_ms/1000).toFixed(1)}s)`);
  currentStage=null;return effective;
}
const base={startHz:590e6,stopHz:600e6,pointCount:1001,rbwHz:10000,controls:{attenuationDb:.5,ifGainDb:11},traceMode:'clear-write'};
try {
  port=await new Promise((resolvePort,reject)=>{
    const timer=setTimeout(()=>reject(new Error('No shell handshake in 10s')),10000);
    child.once('error',error=>{clearTimeout(timer);reject(error);});
    child.once('exit',code=>{clearTimeout(timer);reject(new Error(`Plugin exited ${code}: ${stderr}`));});
    child.stdout.on('data',chunk=>{logs=(logs+chunk).slice(-64000);const line=logs.split('\n').find(v=>v.startsWith('SB_PLUGIN_READY '));if(line){clearTimeout(timer);resolvePort(JSON.parse(line.slice(16)).port);}});
  });
  report.port=port;
  eventTask=collectEvents();
  assert.equal((await api('/health')).ok,true);
  await api('/config','PUT',{values:{windowsBackend:'native'}});
  const {devices}=await api('/devices');assert.equal(devices.length,1,'Connect exactly one supported RSA306B');
  const device=devices[0];assert.ok(device.id.startsWith('usb:rsa306b:')&&!device.id.includes('MOCK'),'Real RSA306B serial required');
  report.device_id=device.id;devicePath=`/devices/${encodeURIComponent(device.id)}`;
  if(mode==='acquisition') {
    await stage('microphone-band',{...base,startHz:470e6,stopHz:694e6},5);
    await stage('LF-only',{...base,startHz:9000,stopHz:1e6,pointCount:401,rbwHz:1000},3);
    await stage('LF-RF-boundary',{...base,startHz:21.75e6,stopHz:22.25e6,pointCount:401,rbwHz:1000},3);
    await stage('full-range',{...base,startHz:9000,stopHz:6.2e9},3);
    await stage('active-retune',base,3);
  } else if(mode==='controls') {
    await stage('baseline',base,2);
    await stage('controls-clamp-low',{controls:{attenuationDb:-100,ifGainDb:-100}},2,c=>assert.deepEqual(c.controls,{attenuationDb:0,ifGainDb:-11}));
    await stage('controls-clamp-high',{controls:{attenuationDb:100,ifGainDb:100}},2,c=>assert.deepEqual(c.controls,{attenuationDb:31,ifGainDb:20}));
    await stage('half-dB-quantization',{controls:{attenuationDb:8.2,ifGainDb:10.7}},2,c=>assert.deepEqual(c.controls,{attenuationDb:8,ifGainDb:10.5}));
    await stage('partial-control-retains-other',{controls:{attenuationDb:12}},2,c=>assert.deepEqual(c.controls,{attenuationDb:12,ifGainDb:10.5}));
    await stage('manual-RBW',{...base,rbwHz:3000},2,c=>assert.ok(c.rbwHz>0&&c.rbwHz<=3000));
    await stage('automatic-RBW',{rbwHz:null},2,c=>{assert.equal(c.rbwHz,undefined);assert.ok(c.resolved.rbwHz>0);});
    await stage('max-hold',{traceMode:'max-hold'},3);
    await stage('min-hold',{traceMode:'min-hold'},3);
    await stage('average',{traceMode:'average'},3);
    await stage('restore-defaults',base,2);
  } else if(mode==='soak') {
    await stage('microphone-band-soak',{...base,startHz:470e6,stopHz:694e6},3,()=>{},seconds);
    report.soak_seconds=seconds;
  } else if(mode==='disconnect') {
    // A host-owned row survives discovery disappearance, exposing failed status.
    const serial=device.id.slice('usb:rsa306b:'.length);
    await api('/devices','POST',{id:device.id,product:device.product,config:{serial}});
    await stage('before-physical-disconnect',{...base,startHz:470e6,stopHz:694e6},3);
    const wait = ms => new Promise(resolveWait=>setTimeout(resolveWait,ms));
    const waitBegin=Date.now();
    currentStage={name:'physical-disconnect-detected',status:'running',started_at:new Date().toISOString(),deadline_ms:180000,poll_interval_ms:500,
      timing_basis:'Time from READY_UNPLUG prompt to failed status; physical USB removal timestamp is not independently measured.',status_samples:[]};
    report.stages.push(currentStage);report.awaiting_action='READY_UNPLUG';persist();
    console.log('READY_UNPLUG: Unplug the RSA306B USB cable now. Leave it unplugged until READY_RECONNECT.');
    let failed;
    while(Date.now()-waitBegin<180000) {
      if(aborted.signal.aborted)throw aborted.signal.reason;
      const listing=await api('/devices','GET',undefined,{timeoutMs:Math.min(10000,180000-(Date.now()-waitBegin))});
      const row=listing.devices.find(value=>value.id===device.id);
      assert.ok(row,'Host-owned device row disappeared before failure could be observed');
      const sample={at:new Date().toISOString(),status:row.status};
      if(!currentStage.status_samples.length||JSON.stringify(currentStage.status_samples.at(-1).status)!==JSON.stringify(row.status)) {
        currentStage.status_samples.push(sample);persist();
      }
      if(row.status?.status==='failed'){failed=row;break;}
      await wait(500);
    }
    assert.ok(failed,'No failed device status within 180s of READY_UNPLUG');
    currentStage.failure_status=failed.status;
    currentStage.prompt_to_failure_ms=Date.now()-waitBegin;
    currentStage.status='passed';report.awaiting_action=null;persist();
    console.log(`passed: physical-disconnect-detected (${currentStage.prompt_to_failure_ms}ms from prompt)`);
    currentStage=null;
    assert.equal((await api('/health')).ok,true,'Shell health failed after physical disconnect');
    await api(devicePath,'DELETE');devicePath=null;
    const emptyBegin=Date.now();let emptyListing;
    while(Date.now()-emptyBegin<30000) {
      const listing=await api('/devices','GET',undefined,{timeoutMs:10000});
      if(listing.devices.length===0){emptyListing=listing;break;}
      await wait(500);
    }
    assert.ok(emptyListing,'Discovery did not become empty within 30s after removal');
    assert.equal((await api('/health')).ok,true,'Shell health failed in no-device state');
    report.stages.push({name:'no-device-discovery-and-health',status:'passed',discovery:emptyListing,settle_ms:Date.now()-emptyBegin});persist();
    const reconnectBegin=Date.now();
    currentStage={name:'physical-reconnect-discovered',status:'running',started_at:new Date().toISOString(),deadline_ms:180000,poll_interval_ms:1000,
      timing_basis:'Time from READY_RECONNECT prompt to rediscovery; physical USB connection timestamp is not independently measured.'};
    report.stages.push(currentStage);report.awaiting_action='READY_RECONNECT';persist();
    console.log('READY_RECONNECT: Reconnect the RSA306B USB cable now.');
    let reconnected;
    while(Date.now()-reconnectBegin<180000) {
      if(aborted.signal.aborted)throw aborted.signal.reason;
      const listing=await api('/devices','GET',undefined,{timeoutMs:Math.min(20000,180000-(Date.now()-reconnectBegin))});
      if(listing.devices.length) {
        assert.equal(listing.devices.length,1,'Exactly one analyzer must return');
        assert.equal(listing.devices[0].id,device.id,'A different analyzer returned');
        reconnected=listing.devices[0];break;
      }
      await wait(1000);
    }
    assert.ok(reconnected,'Same analyzer was not rediscovered within 180s of READY_RECONNECT');
    currentStage.device=reconnected;currentStage.prompt_to_rediscovery_ms=Date.now()-reconnectBegin;currentStage.status='passed';
    report.awaiting_action=null;persist();currentStage=null;
    await api('/devices','POST',{id:device.id,product:device.product,config:{serial}});
    devicePath=`/devices/${encodeURIComponent(device.id)}`;
    await stage('fresh-session-after-physical-reconnect',{...base,startHz:470e6,stopHz:694e6},3);
    report.pending=report.pending.filter(value=>value!=='physical unplug/reconnect');persist();
  } else {
    await stage('initial-open',base,2);
    // An alias with the same explicit serial exercises the in-plugin exclusive-owner guard.
    const alias=competingAlias;aliasPath=`/devices/${encodeURIComponent(alias)}`;
    await api('/devices','POST',{id:alias,product:device.product,config:{serial:device.id.slice('usb:rsa306b:'.length)}});
    const competing=await api(aliasPath+'/configuration','POST',base,{allowError:true});
    assert.equal(competing.status,503);assert.match(JSON.stringify(competing.body),/already open/i);
    report.stages.push({name:'competing-owner-rejected',status:'passed',response:competing});
    await api(aliasPath,'DELETE');aliasPath=null;persist();
    for(let i=1;i<=3;i++) {
      const stopped=await api(devicePath+'/sweep/stop','POST');assert.equal(stopped.sweeping,false);
      assert.equal((await api(devicePath+'/sweep/stop','POST')).sweeping,false,'Repeated stop must be safe');
      await stage(`stop-start-${i}`,base,2);
    }
    for(let i=1;i<=2;i++) {
      await api(devicePath,'DELETE');devicePath=null;
      // Explicit add avoids a cached discovery row reappearing on its own.
      await api('/devices','POST',{id:device.id,product:device.product,config:{serial:device.id.slice('usb:rsa306b:'.length)}});
      devicePath=`/devices/${encodeURIComponent(device.id)}`;
      await stage(`close-reopen-${i}`,base,2);
    }
  }
  assert.equal((await api('/health')).ok,true);
  await api(devicePath+'/sweep/stop','POST');
  report.final_devices=(await api('/devices')).devices;
  await api(devicePath,'DELETE');devicePath=null;
  if(mode!=='disconnect')assert.equal(classifyFailedEvents().length,0,'Unexpected failed device status occurred');
  if(mode!=='soak'||seconds>=1800)report.pending=report.pending.filter(value=>value!==modeGate[mode]);
  report.automated_status='passed';
} catch(error) {
  if(currentStage){delete currentStage.previous_amplitudes;currentStage.status='failed';currentStage.error=error.message;}
  report.status='failed';report.automated_status='failed';report.error=error.message;
  process.stderr.write(`${error.stack || error.message}\n`);process.exitCode=1;
} finally {
  if(aliasPath&&port)try{await api(aliasPath,'DELETE',undefined,{cleanup:true,timeoutMs:40000});}catch(error){report.alias_cleanup_error=error.message;}
  if(devicePath&&port)try{await api(devicePath,'DELETE',undefined,{cleanup:true,timeoutMs:40000});}catch(error){report.cleanup_error=error.message;}
  eventsAbort.abort();await eventTask;
  if(mode!=='disconnect'&&classifyFailedEvents().length) {
    report.status='failed';report.automated_status='failed';
    report.error??='Unexpected failed device status occurred';process.exitCode=1;
  }
  if(!exited && process.platform==='win32') {
    // Windows does not deliver SIGTERM to Node as a graceful signal. Device
    // DELETE above first waits for worker cleanup; terminate the remaining shell
    // as a tree so an error path cannot leave a child behind after its parent dies.
    report.plugin_shutdown='Windows process-tree termination after device cleanup';
    await new Promise(resolveKill=>execFile('taskkill',['/pid',String(child.pid),'/f','/t'],{windowsHide:true,timeout:15000},error=>{
      if(error&&!exited)report.cleanup_error=`Windows process-tree shutdown failed: ${error.message}`;
      resolveKill();
    }));
  } else if(!exited)child.kill('SIGTERM');
  let timer;await Promise.race([done,new Promise(resolveWait=>{timer=setTimeout(resolveWait,30000);})]);clearTimeout(timer);
  if(!exited){child.kill('SIGKILL');report.cleanup_error='Plugin required forced shutdown; verify its owned workers have exited before reconnecting.';}
  if(report.cleanup_error||report.alias_cleanup_error||report.events_error){report.status='failed';report.automated_status='failed';process.exitCode=1;}
  report.finished_at=new Date().toISOString();if(stderr)report.stderr=stderr;if(report.automated_status!=='passed')report.plugin_log=logs;
  clearTimeout(deadline);persist();
  console.log(`Evidence: ${output}; automated=${report.automated_status}; overall=${report.status}`);
}
