#!/usr/bin/env node
// Check local plugin prerequisites without networking or USB access.
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = new URL('../', import.meta.url);
const read = name => JSON.parse(readFileSync(new URL(name, root), 'utf8'));
let failures = 0;
async function check(name, run) {
  try { await run(); console.log(`  ok    ${name}`); }
  catch (error) { failures++; console.error(`  FAIL  ${name}: ${error.message}`); }
}

await check('Node runtime', () => {
  assert.ok(Number(process.versions.node.split('.')[0]) >= 20, 'Node 20 or newer is required');
});
await check('package identity and pinned SDK', () => {
  const pkg = read('package.json'), lock = read('package-lock.json'), manifest = read('soundbase-plugin.json');
  assert.equal(pkg.name, 'soundbase-plugin-rsa306b');
  assert.equal(manifest.id, 'rsa306b');
  assert.equal(pkg.version, manifest.version);
  assert.equal(lock.version, pkg.version);
  assert.equal(lock.packages[''].version, pkg.version);
  for (const name of ['plugin-contract', 'plugin-shell']) {
    const key = `@soundbase/${name}`;
    const installed = read(`node_modules/${key}/package.json`).version;
    assert.equal(installed, pkg.dependencies[key], `${key}: run npm ci`);
    assert.equal(lock.packages[`node_modules/${key}`].version, installed);
  }
});
await check('SoundBase manifest and contract', async () => {
  const contract = await import('@soundbase/plugin-contract');
  const manifest = read('soundbase-plugin.json');
  const result = contract.validateManifest(manifest);
  assert.ok(result.ok, result.errors?.join('; '));
  assert.equal(manifest.contract.core, contract.CORE_CONTRACT_VERSION);
  assert.equal(manifest.contract.modules.SpectrumAnalyzer, contract.SA_MODULE_VERSION);
  assert.deepEqual([...manifest.platforms].sort(), ['darwin-arm64', 'win32-x64']);
  assert.deepEqual(manifest.runtime, { type: 'node', entrypoint: 'main.js' });
  assert.ok(existsSync(new URL(manifest.runtime.entrypoint, root)));
});
await check('spectrum analyzer adapter', async () => {
  const adapter = await import(new URL('adapter.js', root).href);
  assert.equal(typeof adapter.discoverDevices, 'function');
  assert.equal(typeof adapter.createSpectrumAnalyzerAdapter, 'function');
  assert.ok(read('soundbase-plugin.json').products.some(p => p.deviceTypeId === adapter.PRODUCT));
});
await check('license notices', () => {
  for (const name of ['LICENSE', 'LICENSE-SOUNDBASE', 'THIRD-PARTY-NOTICES.md']) {
    assert.ok(existsSync(new URL(name, root)), `Missing ${name}`);
  }
});
console.log(`\n${failures} failure(s). Plugin: ${fileURLToPath(root)}`);
process.exitCode = failures ? 1 : 0;
