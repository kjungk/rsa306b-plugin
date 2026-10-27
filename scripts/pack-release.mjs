#!/usr/bin/env node
// Assemble the one ZIP accepted by SoundBase Lab. Generated native assets are
// explicit inputs, not inferred from git ls-files. No installation or USB I/O.
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { copyFileSync, existsSync, lstatSync, mkdirSync, mkdtempSync, readFileSync,
  readdirSync, rmSync, writeFileSync, chmodSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = fileURLToPath(new URL('..', import.meta.url));
const args = process.argv.slice(2);
const option = name => { const i = args.indexOf(name); return i < 0 ? null : args[i + 1]; };
const expected = option('--expect');
const out = resolve(ROOT, option('--out') ?? 'dist');
const python = process.env.PYTHON ?? (process.platform === 'win32' ? 'python' : 'python3');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const readJSON = file => JSON.parse(readFileSync(file, 'utf8'));
const run = (command, argv, options = {}) => {
  const r = spawnSync(command, argv, { cwd: ROOT, stdio: 'inherit', ...options });
  if (r.error || r.status !== 0) throw new Error(`${command} ${argv.join(' ')} failed: ${r.error ?? r.status}`);
};
const git = (...argv) => {
  const r = spawnSync('git', argv, { cwd: ROOT, encoding: 'utf8' });
  return r.status === 0 ? r.stdout.trim() : null;
};
const allFiles = dir => readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0).flatMap(entry => {
  const path = join(dir, entry.name);
  if (entry.isSymbolicLink()) throw new Error(`Symlink is not a portable package input: ${path}`);
  return entry.isDirectory() ? allFiles(path) : entry.isFile() ? [path] : [];
});
const copy = (source, destination) => {
  const s = lstatSync(source);
  if (!s.isFile() || s.isSymbolicLink()) throw new Error(`Expected a regular file: ${source}`);
  mkdirSync(dirname(destination), { recursive: true });
  copyFileSync(source, destination);
  chmodSync(destination, s.mode & 0o111 ? 0o755 : 0o644);
};
const copyTree = (source, destination, include = () => true) => {
  for (const path of allFiles(source)) {
    const rel = relative(source, path).replaceAll('\\', '/');
    if (include(rel)) copy(path, join(destination, rel));
  }
};

const manifest = readJSON(join(ROOT, 'soundbase-plugin.json'));
const pkg = readJSON(join(ROOT, 'package.json'));
const lock = readJSON(join(ROOT, 'package-lock.json'));
if (manifest.id !== 'rsa306b' || manifest.version !== pkg.version || lock.version !== pkg.version || !manifest.license) {
  throw new Error('Manifest/package/lock identity, version or license mismatch');
}
if (JSON.stringify([...manifest.platforms].sort()) !== JSON.stringify(['darwin-arm64', 'win32-x64'])) {
  throw new Error('The release must contain exactly macOS arm64 and Windows x64');
}
if (expected && (!/^\d+\.\d+\.\d+$/.test(expected) || manifest.version !== expected)) {
  throw new Error('--expect must match the three-part manifest version');
}
const dirty = git('status', '--porcelain') !== '';
if (expected && dirty) throw new Error('Release verification requires committed source; use a rehearsal pack for local changes');
const archive = join(out, `${manifest.id}-${manifest.version}.zip`);
if (existsSync(archive) || existsSync(`${archive}.sha256`)) throw new Error(`Output exists: ${archive}; choose --out for another candidate`);
mkdirSync(out, { recursive: true });
const work = mkdtempSync(join(tmpdir(), 'rsa306b-package-'));
const stage = join(work, 'rsa306b');
mkdirSync(stage);
try {
  // Only public plugin code, recipient documentation, notices, and rebuildable
  // native source travel. Private captures and vendor binaries have no input path.
  for (const name of ['main.js', 'adapter.js', 'soundbase-plugin.json', 'package-lock.json',
    'README.md', 'LICENSE', 'LICENSE-TEMPLATE', 'UPSTREAM.md']) copy(join(ROOT, name), join(stage, name));
  copyTree(join(ROOT, 'driver'), join(stage, 'driver'), rel => /\.(js|py|json)$/.test(rel) && !rel.split('/').some(p => ['__tests__', 'tests', '__pycache__'].includes(p)));
  copy(join(ROOT, 'release/THIRD-PARTY-NOTICES.md'), join(stage, 'THIRD-PARTY-NOTICES.md'));
  copy(join(ROOT, 'release/README.md'), join(stage, 'RELEASE.md'));
  copy(join(ROOT, 'release/README.md'), join(stage, 'release/README.md'));
  copy(join(ROOT, 'release/runtime-pins.json'), join(stage, 'release/runtime-pins.json'));
  if (existsSync(join(ROOT, 'docs'))) copyTree(join(ROOT, 'docs'), join(stage, 'docs'), rel => rel.endsWith('.md'));
  for (const target of ['darwin-arm64', 'win32-x64']) {
    const runtime = join(ROOT, 'runtime', target);
    const info = readJSON(join(runtime, 'runtime.json'));
    if (info.target !== target) throw new Error(`Wrong runtime target: ${target}`);
    const entries = allFiles(runtime).map(path => relative(runtime, path).replaceAll('\\', '/')).filter(path => path !== 'runtime.json');
    if (JSON.stringify(entries.sort()) !== JSON.stringify(Object.keys(info.files_sha256).sort())) throw new Error(`Runtime manifest coverage mismatch: ${target}`);
    for (const rel of entries) {
      if (hash(readFileSync(join(runtime, rel))) !== info.files_sha256[rel]) throw new Error(`Runtime changed after assembly: ${target}/${rel}`);
    }
    copyTree(runtime, join(stage, 'runtime', target));
  }
  // The public npm lockfile describes the entire production closure. Copy only
  // those packages, never an SDK from the installed SoundBase application.
  for (const [rel, entry] of Object.entries(lock.packages)) {
    if (!rel || entry.dev) continue;
    if (!rel.startsWith('node_modules/') || rel.split('/').includes('..') || !entry.resolved?.startsWith('https://registry.npmjs.org/') || !entry.integrity) {
      throw new Error(`Dependency is not pinned to public npm: ${rel}`);
    }
    if (readJSON(join(ROOT, rel, 'package.json')).version !== entry.version) throw new Error(`Installed dependency differs from lockfile: ${rel}`);
    copyTree(join(ROOT, rel), join(stage, rel), path => !path.split('/').includes('.bin'));
  }
  copy(join(ROOT, 'native/CMakeLists.txt'), join(stage, 'sources/native/CMakeLists.txt'));
  for (const name of ['README.md', 'LICENSE']) copy(join(ROOT, 'native', name), join(stage, 'sources/native', name));
  for (const dir of ['include', 'src', 'tools', 'tests']) {
    copyTree(join(ROOT, 'native', dir), join(stage, 'sources/native', dir), rel =>
      !rel.split('/').includes('__pycache__') && /\.(cpp|hpp|h|py|json|csv|bin|txt)$/.test(rel));
  }
  for (const name of ['build-native.py', 'build-native-windows.py', 'assemble-runtime.py', 'runtime-common.py']) copy(join(ROOT, 'scripts', name), join(stage, 'sources/scripts', name));
  copy(join(ROOT, 'release/runtime-pins.json'), join(stage, 'sources/release/runtime-pins.json'));
  copy(join(ROOT, 'LICENSE'), join(stage, 'sources/LICENSE'));
  const content = createHash('sha256');
  for (const path of allFiles(stage)) {
    content.update(relative(stage, path).replaceAll('\\', '/')).update('\0').update(readFileSync(path));
  }
  const packed = { ...pkg, scripts: { start: 'node main.js' } };
  delete packed.devDependencies;
  packed.soundbase = { ...pkg.soundbase, packedFrom: `${pkg.name}@${pkg.version}`,
    contentHash: `sha256:${content.digest('hex')}`, commit: git('rev-parse', 'HEAD'), dirty,
    contract: manifest.contract, contractPackageVersion: '0.12.2', shellVersion: '0.12.2' };
  writeFileSync(join(stage, 'package.json'), `${JSON.stringify(packed, null, 2)}\n`);
  writeFileSync(join(stage, 'SHA256SUMS'), allFiles(stage).map(path => `${hash(readFileSync(path))}  ${relative(stage, path).replaceAll('\\', '/')}\n`).join(''));
  run(python, [join(ROOT, 'scripts/zip-release.py'), stage, archive]);
  const extracted = join(work, 'extracted');
  run(python, [join(ROOT, 'scripts/verify-release.py'), archive, '--extract', extracted, '--output', join(out, 'package-report.json')]);
  const supported = ['darwin-arm64', 'win32-x64'].includes(`${process.platform}-${process.arch}`);
  run(process.execPath, [join(ROOT, 'scripts/check-package.mjs'), join(extracted, 'rsa306b'), ...(supported ? [] : ['--portable'])]);
  writeFileSync(`${archive}.sha256`, `${hash(readFileSync(archive))}  ${manifest.id}-${manifest.version}.zip\n`);
  console.log(`Candidate: ${archive}\nBoth runtimes included; publication requires matching qualification evidence.`);
} catch (error) {
  rmSync(archive, { force: true });
  throw error;
} finally {
  rmSync(work, { recursive: true, force: true });
}
