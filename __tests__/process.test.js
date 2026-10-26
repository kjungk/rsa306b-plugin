import test from 'node:test';
import assert from 'node:assert/strict';
import { queryProcess, streamProcess } from '../driver/process.js';

test('one-shot worker rejects invalid JSON, timeout and nonzero exit', async () => {
  await assert.rejects(queryProcess(process.execPath, ['-e', 'console.log("invalid")']), /Invalid worker response/);
  await assert.rejects(queryProcess(process.execPath, ['-e', 'setInterval(()=>{},1000)'], {timeoutMs:50}), /within/);
  await assert.rejects(queryProcess(process.execPath, ['-e', 'process.exit(2)']), /code 2/);
});
test('stream cooperative stop requires acknowledgement and closes its stdin', async () => {
  const code = 'process.stdin.once("data",()=>{console.log(JSON.stringify({type:"stopped"}));process.exit(0)});console.log(JSON.stringify({type:"ready"}));';
  let ready; const waiting = new Promise(r=>ready=r);
  const worker = streamProcess(process.execPath, ['-e',code], ()=>ready(), error=>{throw error;});
  await waiting; await worker.stop(); await worker.stop();
});
test('stream timeout reports once and force-kills a wedged worker', async () => {
  const errors=[];
  const worker=streamProcess(process.execPath,['-e','process.stdin.resume();setInterval(()=>{},1000);'],()=>{},e=>errors.push(e),{idleTimeoutMs:60,stopTimeoutMs:60});
  await worker.done; assert.equal(errors.length,1); await assert.rejects(worker.stop(), /forced termination/);
});
test('stream refuses malformed record and missing cleanup acknowledgement', async () => {
  const errors=[];
  const worker=streamProcess(process.execPath,['-e','console.log("bad json");process.stdin.on("data",()=>process.exit(0));'],()=>{},e=>errors.push(e));
  await worker.done; assert.equal(errors.length,1); await assert.rejects(worker.stop(),/clean device shutdown/);
});
