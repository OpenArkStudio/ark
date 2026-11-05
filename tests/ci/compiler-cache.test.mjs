import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { cachePlan } from '../../.github/scripts/vcpkg-cache.mjs';
import { compilerCachePlan, prepareCompilerCache, sccacheVersion } from '../../.github/scripts/compiler-cache.mjs';

function fixture(t) {
  const root = mkdtempSync(join(tmpdir(), 'ark-compiler-cache-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  mkdirSync(join(root, 'ports'));
  writeFileSync(join(root, 'vcpkg.json'), '{}');
  writeFileSync(join(root, 'vcpkg-configuration.json'), '{"overlay-ports":["ports"]}');
  const env = {
    GITHUB_SHA: 'b'.repeat(40), GITHUB_RUN_ID: '1001', GITHUB_RUN_ATTEMPT: '1',
    GITHUB_EVENT_NAME: 'pull_request', GITHUB_REF: 'refs/pull/7/merge',
    COMPILER_CACHE_PROFILE: 'applications', COMPILER_CACHE_EPOCH: '2026-W45',
    RUNNER_OS: 'Linux', RUNNER_ARCH: 'X64', RUNNER_TEMP: join(root, 'runner temp'),
    ImageOS: 'ubuntu24', ImageVersion: '20261020.1', VCPKG_CACHE_REVISION: 'a'.repeat(40),
    VCPKG_CACHE_FEATURES: 'boost;ssl', VCPKG_CACHE_DEFAULT_FEATURES: 'true',
    VCPKG_CACHE_PROFILE: 'applications', VCPKG_DEFAULT_TRIPLET: 'x64-linux',
    VCPKG_DEFAULT_HOST_TRIPLET: 'x64-linux', GITHUB_ENV: join(root, 'env'),
    GITHUB_OUTPUT: join(root, 'output'), SCCACHE_SETUP_OUTCOME: 'success',
    SCCACHE_PATH: '/tool cache/sccache', SCCACHE_RESTORE_OUTCOME: 'success',
  };
  env.COMPILER_CACHE_IDENTITY = cachePlan({ root, env }).key;
  return { root, env };
}

test('compiler launcher uses only the verified pinned version with cache IO fallback', t => {
  const f = fixture(t);
  const calls = [];
  const plan = prepareCompilerCache({ ...f, run: (command, args, options) => {
    calls.push([command, args]);
    assert.equal(options.env.SCCACHE_GHA_ENABLED, 'false');
    assert.equal(options.env.SCCACHE_IGNORE_SERVER_IO_ERROR, '1');
    assert.match(options.env.SCCACHE_C_CUSTOM_CACHE_BUSTER, /ark-sccache-0.16.0-v2/);
    return { status: 0, stdout: `sccache ${sccacheVersion}\n` };
  } });
  assert.equal(plan.enabled, true);
  assert.equal(plan.launcher, f.env.SCCACHE_PATH);
  assert.deepEqual(calls.map(call => call[1]), [['--version'], ['--start-server']]);
  assert.match(readFileSync(f.env.GITHUB_ENV, 'utf8'), /ARK_COMPILER_LAUNCHER=\/tool cache\/sccache/);
});

test('preparation does not start a server before Actions restores the local cache directory', t => {
  const f = fixture(t);
  const calls = [];
  const plan = prepareCompilerCache({ ...f, start: false, run: (_command, args) => {
    calls.push(args);
    return { status: 0, stdout: `sccache ${sccacheVersion}\n` };
  } });
  assert.equal(plan.enabled, true);
  assert.deepEqual(calls, [['--version']]);
});

test('failed setup, wrong version, startup errors and incomplete identity all use the compiler directly', t => {
  for (const extra of [{ SCCACHE_SETUP_OUTCOME: 'failure' }, { SCCACHE_RESTORE_OUTCOME: 'failure' }, { SCCACHE_PATH: '' },
    { SCCACHE_PATH: '/unsafe;launcher' }, { SCCACHE_PATH: 'relative/launcher' }, { RUNNER_ARCH: 'ARM64' }, { COMPILER_CACHE_IDENTITY: '' }, { COMPILER_CACHE_IDENTITY: 'unsafe\nidentity' },
    { GITHUB_SHA: '' }, { RUNNER_OS: 'Windows' }, { RUNNER_TEMP: '/unsafe;cache' },
    { behavior: 'wrong-version' }, { behavior: 'version-error' }, { behavior: 'startup-error' }, { behavior: 'throw' }]) {
    const f = fixture(t);
    const calls = [];
    const plan = prepareCompilerCache({ env: { ...f.env, ...extra }, run: (_command, args) => {
      calls.push(args);
      if (extra.behavior === 'throw') throw new Error('tool unavailable');
      return { status: extra.behavior === 'version-error' || (extra.behavior === 'startup-error' && args[0] === '--start-server') ? 1 : 0,
        stdout: extra.behavior === 'wrong-version' ? 'sccache 0.1.0' : `sccache ${sccacheVersion}` };
    } });
    assert.equal(plan.enabled, false, JSON.stringify(extra));
    if (!extra.behavior) assert.deepEqual(calls, [], JSON.stringify(extra));
    assert.equal(plan.launcher, '');
    assert.match(readFileSync(f.env.GITHUB_ENV, 'utf8'), /^ARK_COMPILER_LAUNCHER=\n/);
  }
});

test('CMake passes a launcher path with spaces, keeps the compiler and propagates compilation failures', { skip: process.platform === 'win32' }, t => {
  const f = fixture(t);
  const launcher = join(f.root, 'launcher with spaces.mjs');
  const trace = join(f.root, 'launcher-trace');
  // Run a transparent fixture launcher through the same two CMake launcher arguments as CI.
  writeFileSync(launcher, `import { spawnSync } from 'node:child_process';\nimport { appendFileSync } from 'node:fs';\nappendFileSync(${JSON.stringify(trace)}, process.argv[2] + '\\n');\nconst r = spawnSync(process.argv[2], process.argv.slice(3), { stdio: 'inherit' });\nprocess.exit(r.status ?? 1);\n`);
  writeFileSync(join(f.root, 'CMakeLists.txt'), 'cmake_minimum_required(VERSION 3.21)\nproject(launcher LANGUAGES C CXX)\nadd_executable(check main.cpp helper.c)\n');
  writeFileSync(join(f.root, 'main.cpp'), 'int main() { return 0; }\n');
  writeFileSync(join(f.root, 'helper.c'), 'int helper(void) { return 0; }\n');
  const build = join(f.root, 'build');
  const run = args => spawnSync('cmake', args, { encoding: 'utf8', timeout: 30_000 });
  const configured = run(['-S', f.root, '-B', build, '-G', 'Ninja',
    `-DCMAKE_C_COMPILER_LAUNCHER=${process.execPath};${launcher}`,
    `-DCMAKE_CXX_COMPILER_LAUNCHER=${process.execPath};${launcher}`]);
  assert.equal(configured.status, 0, configured.stdout + configured.stderr);
  const compiled = run(['--build', build, '--parallel', '1']);
  assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr);
  const compilers = readFileSync(trace, 'utf8').trim().split('\n');
  assert.equal(compilers.length, 2);
  const cache = readFileSync(join(build, 'CMakeCache.txt'), 'utf8');
  for (const language of ['C', 'CXX']) {
    const compiler = cache.match(new RegExp(`^CMAKE_${language}_COMPILER:FILEPATH=(.+)$`, 'm'))?.[1];
    assert.ok(compiler && compilers.includes(compiler), `${language}: CMake retains the actual compiler`);
  }
  writeFileSync(join(f.root, 'main.cpp'), '#error intentional compiler failure\n');
  const failed = run(['--build', build, '--parallel', '1']);
  assert.notEqual(failed.status, 0);
  assert.match(failed.stdout + failed.stderr, /intentional compiler failure/);
  writeFileSync(join(f.root, 'main.cpp'), 'int main() { return 0; }\n');
  const direct = join(f.root, 'direct');
  assert.equal(run(['-S', f.root, '-B', direct, '-G', 'Ninja',
    '-DCMAKE_C_COMPILER_LAUNCHER=', '-DCMAKE_CXX_COMPILER_LAUNCHER=']).status, 0);
  assert.equal(run(['--build', direct, '--parallel', '1']).status, 0, 'disabled caches compile normally');
  writeFileSync(join(f.root, 'main.cpp'), '#error intentional compiler failure\n');
  assert.notEqual(run(['--build', direct, '--parallel', '1']).status, 0, 'cold builds must preserve compiler errors');
});

test('source commits and run attempts share one weekly immutable snapshot', t => {
  const f = fixture(t);
  const run = () => ({ status: 0, stdout: `sccache ${sccacheVersion}` });
  const first = prepareCompilerCache({ ...f, run });
  const next = prepareCompilerCache({ ...f, env: { ...f.env, GITHUB_SHA: 'c'.repeat(40), GITHUB_RUN_ID: '2002', GITHUB_RUN_ATTEMPT: '2' }, run });
  assert.equal(first.key, next.key);
  assert.equal(first.restoreKeys, next.restoreKeys);
  assert.equal(first.archiveKey, next.archiveKey);
  assert.ok(first.archiveKey.startsWith(next.restoreKeys.split('\n')[0]));
  assert.equal(first.key.includes(f.env.GITHUB_SHA), false);
  assert.equal(first.directory, next.directory);
});

test('image OS, compiler flags, dependency contents and profiles isolate compiler restore prefixes', t => {
  const f = fixture(t);
  const run = () => ({ status: 0, stdout: `sccache ${sccacheVersion}` });
  const baseline = prepareCompilerCache({ ...f, run });
  for (const extra of [{ ImageOS: 'ubuntu26' }, { CXXFLAGS: '-fsanitize=thread' }, { CFLAGS: '-O3' }, { LDFLAGS: '-flto' },
    { VCPKG_DEFAULT_TRIPLET: 'arm64-linux' },
    { CXX: '/other/compiler' }, { VCPKG_CACHE_PROFILE: 'database-Standalone' }, { VCPKG_CACHE_PROFILE: 'network-address' },
    { VCPKG_CACHE_FEATURES: 'database' }, { VCPKG_CACHE_REVISION: 'd'.repeat(40) }]) {
    const identity = cachePlan({ root: f.root, env: { ...f.env, ...extra } }).key;
    const plan = prepareCompilerCache({ ...f, env: { ...f.env, COMPILER_CACHE_IDENTITY: identity }, run });
    assert.notEqual(plan.restoreKeys, baseline.restoreKeys, JSON.stringify(extra));
  }
  writeFileSync(join(f.root, 'vcpkg.json'), '{"dependencies":["zlib"]}');
  const identity = cachePlan(f).key;
  assert.notEqual(prepareCompilerCache({ ...f, env: { ...f.env, COMPILER_CACHE_IDENTITY: identity }, run }).restoreKeys, baseline.restoreKeys);
});

test('unavailable or symbolic-link local storage stays cold and missing command channels fail explicitly', { skip: process.platform === 'win32' }, t => {
  const f = fixture(t);
  writeFileSync(f.env.RUNNER_TEMP, 'not a directory');
  const run = () => ({ status: 0, stdout: `sccache ${sccacheVersion}` });
  assert.equal(prepareCompilerCache({ ...f, run }).enabled, false);
  rmSync(f.env.RUNNER_TEMP);
  mkdirSync(f.env.RUNNER_TEMP);
  symlinkSync(f.root, join(f.env.RUNNER_TEMP, 'ark-sccache'));
  assert.equal(prepareCompilerCache({ ...f, run }).enabled, false);
  for (const name of ['GITHUB_ENV', 'GITHUB_OUTPUT']) {
    assert.throws(() => prepareCompilerCache({ ...f, env: { ...f.env, [name]: '' } }), /file-command/);
  }
});


test('failed restore or startup clears the previously prepared launcher before CMake runs', t => {
  for (const failure of ['restore', 'startup']) {
    const f = fixture(t);
    const ok = () => ({ status: 0, stdout: `sccache ${sccacheVersion}` });
    assert.equal(prepareCompilerCache({ ...f, start: false, run: ok }).enabled, true);
    const env = { ...f.env, SCCACHE_RESTORE_OUTCOME: failure === 'restore' ? 'failure' : 'success' };
    const run = (_command, args) => ({ status: args[0] === '--start-server' ? 1 : 0, stdout: `sccache ${sccacheVersion}` });
    assert.equal(prepareCompilerCache({ env, run }).enabled, false);
    const launchers = readFileSync(f.env.GITHUB_ENV, 'utf8').split('\n').filter(line => line.startsWith('ARK_COMPILER_LAUNCHER='));
    assert.deepEqual(launchers, [`ARK_COMPILER_LAUNCHER=${f.env.SCCACHE_PATH}`, 'ARK_COMPILER_LAUNCHER=']);
    assert.match(readFileSync(f.env.GITHUB_OUTPUT, 'utf8'), /enabled=false\ndirectory=\nkey=\narchive_key=\nsave_enabled=false\ncache_size=512M\nepoch=\narchive_scope=\nrestore_keys=\n$/);
  }
});

test('compatible image versions retain the same object buster and archive family', t => {
  const f = fixture(t);
  const run = () => ({ status: 0, stdout: `sccache ${sccacheVersion}` });
  const first = prepareCompilerCache({ ...f, run });
  const identity = cachePlan({ root: f.root, env: { ...f.env, ImageVersion: '20261108.2' } }).key;
  assert.notEqual(identity, f.env.COMPILER_CACHE_IDENTITY);
  const next = prepareCompilerCache({ env: { ...f.env, COMPILER_CACHE_IDENTITY: identity }, run });
  assert.equal(first.key, next.key);
  assert.equal(first.archiveKey, next.archiveKey);
  assert.equal(first.restoreKeys, next.restoreKeys);
  assert.match(readFileSync(f.env.GITHUB_OUTPUT, 'utf8'), /restore_keys<<ARK_SCCACHE_RESTORE_KEYS\n[^\n]+-untrusted-\n[^\n]+-trusted-\nARK_SCCACHE_RESTORE_KEYS\n/);
});

test('weekly snapshots grow only on epoch change and retain a compatible restore prefix', t => {
  const f = fixture(t);
  delete f.env.COMPILER_CACHE_EPOCH;
  const plan = date => compilerCachePlan({ env: f.env, now: new Date(date) });
  const first = plan('2026-11-02T00:00:00Z');
  const sameWeek = plan('2026-11-08T23:59:59Z');
  const nextWeek = plan('2026-11-09T00:00:00Z');
  assert.equal(first.epoch, '2026-W45');
  assert.equal(first.archiveKey, sameWeek.archiveKey);
  assert.notEqual(first.archiveKey, nextWeek.archiveKey);
  assert.equal(first.key, nextWeek.key);
  assert.equal(first.restoreKeys, nextWeek.restoreKeys);
  assert.equal(plan('2021-01-01T00:00:00Z').epoch, '2020-W53');
});

test('trusted main archives are isolated from untrusted PR snapshots and queue writes', t => {
  const f = fixture(t);
  const plan = extra => compilerCachePlan({ env: { ...f.env, ...extra } });
  const main = plan({ GITHUB_EVENT_NAME: 'push', GITHUB_REF: 'refs/heads/main', COMPILER_CACHE_WRITE: 'true' });
  const pr = plan({});
  const queue = plan({ GITHUB_EVENT_NAME: 'merge_group', GITHUB_REF: 'refs/heads/gh-readonly-queue/main/pr-7' });
  assert.equal(main.saveEnabled, true);
  assert.equal(main.scope, 'trusted');
  assert.doesNotMatch(main.restoreKeys, /untrusted/);
  assert.equal(pr.saveEnabled, false, 'Applications PRs consume the main seed');
  assert.match(pr.restoreKeys, /-untrusted-\n.*-trusted-$/);
  assert.equal(pr.key, main.key, 'archive trust boundaries do not prevent object reuse from a trusted seed');
  assert.equal(queue.saveEnabled, false);
  assert.doesNotMatch(queue.restoreKeys, /untrusted/);
  for (const extra of [
    { COMPILER_CACHE_WRITE: 'true' }, // An untrusted request cannot opt Applications into saving.
    { GITHUB_EVENT_NAME: 'pull_request_target', GITHUB_REF: 'refs/heads/main', COMPILER_CACHE_WRITE: 'true' },
    { GITHUB_EVENT_NAME: 'push', GITHUB_REF: 'refs/heads/topic', COMPILER_CACHE_WRITE: 'true' },
    { GITHUB_EVENT_NAME: 'workflow_dispatch', GITHUB_REF: 'refs/heads/topic', COMPILER_CACHE_WRITE: 'true' },
    { GITHUB_EVENT_NAME: 'push', GITHUB_REF: 'refs/heads/main' },
    { GITHUB_EVENT_NAME: 'push', GITHUB_REF: 'refs/heads/main', COMPILER_CACHE_WRITE: 'false' },
  ]) assert.equal(plan(extra).saveEnabled, false, JSON.stringify(extra));
  assert.equal(plan({ GITHUB_EVENT_NAME: 'workflow_dispatch', GITHUB_REF: 'refs/heads/main', COMPILER_CACHE_WRITE: 'true' }).saveEnabled, true);
});

test('network and database source PRs retain bounded per-ref weekly snapshots', t => {
  const f = fixture(t);
  for (const profile of ['network-none', 'network-address', 'network-thread', 'database-Boost', 'database-Standalone']) {
    const env = { ...f.env, COMPILER_CACHE_PROFILE: profile, VCPKG_CACHE_PROFILE: profile };
    env.COMPILER_CACHE_IDENTITY = cachePlan({ root: f.root, env }).key;
    const first = compilerCachePlan({ env });
    assert.equal(first.saveEnabled, true, profile);
    assert.equal(first.scope, 'untrusted');
    assert.equal(first.cacheSize, '512M');
    assert.equal(first.archiveKey, compilerCachePlan({ env: { ...env, GITHUB_SHA: 'e'.repeat(40) } }).archiveKey);
    assert.equal(compilerCachePlan({ env: { ...env, COMPILER_CACHE_WRITE: 'false' } }).saveEnabled, false);
    assert.equal(compilerCachePlan({ env: { ...env, GITHUB_REF: 'refs/pull/7/merge\n' } }).saveEnabled, false);
    assert.equal(compilerCachePlan({ env: { ...env, GITHUB_EVENT_NAME: 'merge_group' } }).saveEnabled, false);
  }
});

test('cache capacity is bounded, optional, and exported without changing compatibility', t => {
  const f = fixture(t);
  const original = compilerCachePlan(f);
  for (const capacity of ['64', '512', '768', '1024']) {
    const env = { ...f.env, COMPILER_CACHE_MAX_MIB: capacity };
    const seen = [];
    const plan = prepareCompilerCache({ env, run: (_command, _args, options) => {
      seen.push(options.env.SCCACHE_CACHE_SIZE);
      return { status: 0, stdout: `sccache ${sccacheVersion}` };
    } });
    assert.equal(plan.enabled, true);
    assert.equal(plan.cacheSize, `${capacity}M`);
    assert.equal(plan.key, original.key);
    assert.deepEqual(seen, [`${capacity}M`, `${capacity}M`]);
    assert.match(readFileSync(env.GITHUB_ENV, 'utf8'), new RegExp(`SCCACHE_CACHE_SIZE=${capacity}M\\n`));
  }
});

test('invalid capacity, profile, epoch or publication settings disable optional caching safely', t => {
  for (const extra of [
    ...['', '0', '63', '1025', '2048', '-1', '512M', '0.5', '0512', '512\n', '512;bad'].map(COMPILER_CACHE_MAX_MIB => ({ COMPILER_CACHE_MAX_MIB })),
    ...['', '2026-W00', '2026-W54', '2026-W45\n', 'bad'].map(COMPILER_CACHE_EPOCH => ({ COMPILER_CACHE_EPOCH })),
    ...['', 'applications\n', 'unknown'].map(COMPILER_CACHE_PROFILE => ({ COMPILER_CACHE_PROFILE })),
    { COMPILER_CACHE_WRITE: 'maybe' }, { GITHUB_SHA: 'b'.repeat(40) + '\n' },
  ]) {
    const f = fixture(t);
    const plan = prepareCompilerCache({ env: { ...f.env, ...extra }, run: () => assert.fail('invalid cache settings must not launch a tool') });
    assert.equal(plan.enabled, false, JSON.stringify(extra));
    assert.equal(plan.saveEnabled, false);
    assert.equal(plan.launcher, '');
    assert.match(readFileSync(f.env.GITHUB_ENV, 'utf8'), /^ARK_COMPILER_LAUNCHER=\n/);
  }
});
