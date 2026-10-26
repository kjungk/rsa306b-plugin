import { spawn } from 'node:child_process';
import { StringDecoder } from 'node:string_decoder';

const children = new Set();
const MAX_RECORD = 32 * 1024 * 1024;

function forceKill(child) {
  if (!child.pid) return;
  if (process.platform === 'win32') {
    const killer = spawn('taskkill', ['/pid', String(child.pid), '/f', '/t'], { windowsHide: true, stdio: 'ignore' });
    killer.on('error', () => child.kill());
  } else child.kill('SIGKILL');
}

function message(stderr, fallback) {
  try { return String(JSON.parse(stderr.trim()).message || fallback).slice(0, 1500); }
  catch { return String(fallback).slice(0, 1500); }
}
function workerError(stderr, fallback) {
  const error = new Error(message(stderr, fallback));
  try { error.code = JSON.parse(stderr.trim()).code; } catch { /* Non-JSON native diagnostics remain bounded. */ }
  return error;
}

export function queryProcess(executable, args, { timeoutMs = 15000, signal } = {}) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    let stdout = '', stderr = '', failure, closed = false;
    const cancel = () => new Promise(done => {
      if (closed) return done();
      child.once('close', done); failure ??= new Error('Worker was stopped.'); forceKill(child);
    });
    const abort = () => { void cancel(); };
    signal?.addEventListener('abort', abort, { once: true });
    if (signal?.aborted) abort();
    children.add(cancel);
    const timer = setTimeout(() => { failure = new Error(`Worker did not answer within ${timeoutMs / 1000} seconds.`); forceKill(child); }, timeoutMs);
    child.stdout.setEncoding('utf8'); child.stderr.setEncoding('utf8');
    child.stdout.on('data', chunk => {
      stdout += chunk;
      if (stdout.length > MAX_RECORD) { failure = new Error('Worker response exceeded 32 MiB.'); forceKill(child); }
    });
    child.stderr.on('data', chunk => { stderr = (stderr + chunk).slice(-8192); });
    child.on('error', error => { failure = error; });
    child.on('close', code => {
      closed = true; clearTimeout(timer); children.delete(cancel); signal?.removeEventListener('abort', abort);
      if (failure || code !== 0) return reject(workerError(stderr, failure?.message || `Worker exited with code ${code}.`));
      try {
        const lines = stdout.trim().split('\n');
        if (lines.length !== 1 || !lines[0]) throw new Error('Expected exactly one worker response.');
        resolve(JSON.parse(lines[0]));
      } catch (error) { reject(new Error(`Invalid worker response: ${error.message}`)); }
    });
  });
}

// Every worker has a private stdin. Stop and parent EOF permit native cleanup;
// a deadline still kills a process blocked inside a vendor call.
export function streamProcess(executable, args, onRecord, onError, options = {}) {
  const child = spawn(executable, args, { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
  const decoder = new StringDecoder('utf8');
  let buffer = '', stderr = '', stopping = false, closed = false, forced = false, failure, ack = false;
  let exitCode, idleTimer, killTimer, resolveDone;
  const done = new Promise(resolve => { resolveDone = resolve; });
  const cancel = () => stop().catch(() => {});
  children.add(cancel);
  const fail = error => {
    if (failure) return;
    failure = error;
    if (!stopping) onError(error);
    void cancel();
  };
  const progress = () => {
    clearTimeout(idleTimer);
    if (!closed && !stopping) idleTimer = setTimeout(() => fail(new Error('Acquisition worker stopped producing records.')), options.idleTimeoutMs ?? 30000);
  };
  async function stop() {
    if (!stopping && !closed) {
      stopping = true; clearTimeout(idleTimer);
      child.stdin.end('stop\n');
      killTimer = setTimeout(() => { forced = true; forceKill(child); }, options.stopTimeoutMs ?? 5000);
    }
    await done;
    if (forced) throw new Error('Worker required forced termination. Reconnect the analyzer before retrying.');
    if (exitCode !== 0 || !ack) throw new Error(message(stderr, 'Worker did not confirm clean device shutdown.'));
  }
  child.stdin.on('error', error => { if (!stopping) fail(error); });
  child.stdout.on('data', chunk => {
    try {
      buffer += decoder.write(chunk);
      let end;
      while ((end = buffer.indexOf('\n')) >= 0) {
        if (end > MAX_RECORD) throw new Error('Worker JSONL record exceeds 32 MiB.');
        const line = buffer.slice(0, end); buffer = buffer.slice(end + 1);
        if (line.trim()) {
          const record = JSON.parse(line);
          if (record.type === 'stopped') ack = true;
          if (!stopping) onRecord(record);
        }
        progress();
      }
      if (buffer.length > MAX_RECORD) throw new Error('Worker JSONL record exceeds 32 MiB.');
    } catch (error) { fail(error); }
  });
  child.stderr.on('data', chunk => { stderr = (stderr + chunk.toString('utf8')).slice(-8192); });
  child.on('error', fail);
  child.on('close', code => {
    closed = true; exitCode = code; clearTimeout(idleTimer); clearTimeout(killTimer); children.delete(cancel);
    buffer += decoder.end();
    if (!stopping && !failure) fail(new Error(message(stderr, buffer.trim() ? 'Worker ended inside a JSONL record.' : `Acquisition worker exited unexpectedly (${code}).`)));
    resolveDone();
  });
  progress();
  return { stop, done, pid: child.pid };
}

export function shutdownWorkers() { return Promise.allSettled([...children].map(cancel => cancel())); }
