import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { runInNewContext } from 'node:vm';
import test from 'node:test';
import { applicationCommands } from '../../.github/scripts/ci-applications.mjs';
import { requireArchives, requireAvailableSeed } from '../../.github/scripts/vcpkg-seed-check.mjs';

const read = file => readFileSync(new URL(`../../${file}`, import.meta.url), 'utf8').replaceAll('\r\n', '\n');
const seed = read('.github/workflows/vcpkg-seed.yml');
const ci = read('.github/workflows/ci.yml');
const applications = ci.split(/(?=^  [a-z][a-z0-9_-]*:$)/m)
  .find(block => block.startsWith('  applications:\n'));

function steps(workflow) {
  return workflow.split(/(?=^      - )/m).slice(1);
}

function onlyStep(workflow, match) {
  const found = steps(workflow).filter(step => step.includes(match));
  assert.equal(found.length, 1, `expected one ${match}`);
  return found[0];
}

test('only dependency/cache input pushes to main and explicit manual runs can seed', () => {
  const triggers = seed.match(/^on:\n([\s\S]*?)(?=^permissions:)/m)?.[1];
  assert.ok(triggers);
  assert.deepEqual([...triggers.matchAll(/^  (\w+):/gm)].map(match => match[1]), ['push', 'workflow_dispatch']);
  assert.match(triggers, /^  push:\n    branches: \[main\]\n    paths:\n/m);
  const paths = [...triggers.matchAll(/^      - ['"]?([^'"\n]+)['"]?$/gm)].map(match => match[1]);
  assert.deepEqual(paths, ['vcpkg.json', 'vcpkg-configuration.json', 'ports/**', '.gitattributes',
    '.github/scripts/vcpkg-cache.mjs', '.github/scripts/vcpkg-seed-check.mjs', '.github/workflows/vcpkg-seed.yml']);
  assert.doesNotMatch(triggers, /inputs:|schedule:|pull_request|merge_group|workflow_run|repository_dispatch/);
  assert.doesNotMatch(ci, /^  push:/m, 'keep required CI admission unchanged');
});

test('manual runs on another branch cannot write a seed and only read-only checkout access is requested', () => {
  const guard = seed.match(/^    if: \$\{\{ (.+) \}\}$/m)?.[1];
  assert.ok(guard);
  for (const ref of ['refs/heads/main', 'refs/heads/feature', 'refs/pull/1/merge',
    'refs/heads/gh-readonly-queue/main/pr-1-candidate', 'refs/tags/main']) {
    assert.equal(runInNewContext(guard, { github: { ref } }), ref === 'refs/heads/main', ref);
  }
  assert.match(seed, /^permissions:\n  contents: read\n/m);
  assert.equal([...seed.matchAll(/^ *permissions:/gm)].length, 1);
  assert.match(onlyStep(seed, 'uses: actions/checkout@'), /persist-credentials: false/);
  assert.doesNotMatch(seed, /secrets\.|secrets:|cache-mode:|pull_request_target|gh |gh api|curl /);
  assert.match(seed, /^concurrency:\n  group: \$\{\{ github.workflow \}\}-main-windows-applications\n  cancel-in-progress: false$/m);
  assert.equal([...seed.matchAll(/^  [a-z][a-z0-9_-]*:\n    name:/gm)].length, 1);
  assert.match(seed, /^    runs-on: windows-latest$/m);
});

test('seed identity, package provider and tools exactly match Windows Applications CI', () => {
  // New inherited compiler/triplet overrides also need an explicit seed review.
  assert.doesNotMatch(ci.split(/^jobs:/m)[0], /^env:/m);
  const settings = applications.split(/^    steps:/m)[0];
  const inherited = settings.match(/^    env:\n((?:      .+\n)+)/m)?.[1];
  assert.ok(inherited);
  assert.deepEqual([...inherited.matchAll(/^      (\w+):/gm)].map(match => match[1]), [
    'ARK_APPLICATION_PLAN', 'ARK_APPLICATION_SCOPE_RESULT', 'ARK_APPLICATION_VALIDATION_STAGE',
    'ARK_APPLICATION_FILES_KNOWN', 'COMPILER_CACHE_MAX_MIB',
  ], 'only reviewed scheduling/cache-size inputs may differ from the dependency-only seed');
  for (const match of ['id: vcpkg-cache\n', 'id: vcpkg-restore\n',
    'uses: lukka/get-cmake@', 'uses: lukka/run-vcpkg@']) {
    assert.equal(onlyStep(seed, match).trimEnd(), onlyStep(applications, match).trimEnd(), match);
  }
  for (const action of ['ilammy/msvc-dev-cmd', 'actions/setup-node']) {
    const seedStep = onlyStep(seed, `uses: ${action}@`);
    const ciStep = onlyStep(applications, `uses: ${action}@`);
    assert.equal(seedStep.replace(/^        if: runner.os == 'Windows'\n/m, '').trimEnd(),
      ciStep.replace(/^        if: runner.os == 'Windows'\n/m, '').trimEnd());
  }
  const positions = ['id: vcpkg-cache\n', 'id: vcpkg-restore\n', 'uses: lukka/run-vcpkg@',
    'name: Configure applications\n', 'name: Require dependency archive inputs\n',
    'name: Save vcpkg package archives\n', 'id: seed-verify\n', 'name: Require exact dependency cache availability\n'];
  const order = positions.map(match => steps(seed).indexOf(onlyStep(seed, match)));
  assert.deepEqual(order, [...order].sort((a, b) => a - b));
});

test('the complete Windows configuration stays equal and fatal without building ark targets', () => {
  const configure = onlyStep(seed, 'name: Configure applications\n');
  const windowsConfigure = onlyStep(applications, 'name: Configure applications\n');
  assert.match(windowsConfigure, /^        run: node \.github\/scripts\/ci-applications\.mjs configure$/m);
  for (const step of [configure, windowsConfigure]) assert.doesNotMatch(step, /^        (?:if|continue-on-error):/m);
  const root = join(tmpdir(), 'tool cache', 'vcpkg');
  const lines = configure.trimEnd().split('\n');
  const command = lines.findIndex(line => line.startsWith('          cmake '));
  assert.ok(command >= 0);
  assert.equal(lines[command].trim(), 'cmake -S . -B ci-build -G Ninja');
  const seedArgs = ['-S', '.', '-B', 'ci-build', '-G', 'Ninja', ...lines.slice(command + 1).map(line => {
    assert.match(line, /^          (?:"-D[^"\n]+"|-D[^\s]+)$/);
    return line.trim().replace(/^"|"$/g, '').replaceAll('$env:VCPKG_ROOT', root).replaceAll('$env:ARK_COMPILER_LAUNCHER', '');
  })];
  const pools = new Set(['-DCMAKE_JOB_POOLS=ark_compile=2;ark_link=1',
    '-DCMAKE_JOB_POOL_COMPILE=ark_compile', '-DCMAKE_JOB_POOL_LINK=ark_link']);
  const [runnerArgs] = applicationCommands('configure', { RUNNER_OS: 'Windows', VCPKG_ROOT: root,
    GITHUB_EVENT_NAME: 'merge_group', ARK_COMPILER_LAUNCHER: '' });
  const normalizeToolchain = arg => arg.startsWith('-DCMAKE_TOOLCHAIN_FILE=') ? arg.replaceAll('\\', '/') : arg;
  assert.deepEqual(runnerArgs.filter(arg => !pools.has(arg)).map(normalizeToolchain), seedArgs.map(normalizeToolchain),
    'dependency features, Debug flags and every application build option remain identical');
  assert.match(seed, /^    env:\n      ARK_COMPILER_LAUNCHER: ''$/m);
  assert.doesNotMatch(seed, /cmake --build|ctest|sccache|compiler-cache\.mjs|CMAKE_(?:C|CXX)_FLAGS|\/(?:Zi|Z7)\b/);
  assert.doesNotMatch(seed, /VCPKG_DISABLE_COMPILER_TRACKING|VCPKG_ENV_PASSTHROUGH_UNTRACKED|--editable|upload-artifact/);
  assert.equal(steps(seed).filter(step => /uses: actions\/cache\//.test(step)).length, 3);
});

test('only the seed requires archives, fatal saving and verified exact availability', () => {
  const input = onlyStep(seed, 'name: Require dependency archive inputs\n');
  const save = onlyStep(seed, 'name: Save vcpkg package archives\n');
  const verify = onlyStep(seed, 'id: seed-verify\n');
  const available = onlyStep(seed, 'name: Require exact dependency cache availability\n');
  for (const step of [input, save, verify, available]) assert.doesNotMatch(step, /continue-on-error:/);
  for (const step of [input, available]) assert.doesNotMatch(step, /^        if:/m);
  assert.match(input, /VCPKG_ARCHIVE_ENABLED: \$\{\{ steps.vcpkg-cache.outputs.enabled \}\}/);
  assert.match(input, /VCPKG_ARCHIVE_DIRECTORY: \$\{\{ steps.vcpkg-cache.outputs.directory \}\}/);
  assert.match(input, /VCPKG_ARCHIVE_KEY: \$\{\{ steps.vcpkg-cache.outputs.key \}\}/);
  assert.equal(save.split(/^        uses:/m)[1].split(/^      #/m)[0].trimEnd(),
    onlyStep(applications, 'name: Save vcpkg package archives\n').split(/^        uses:/m)[1].trimEnd());
  assert.match(save, /^        if: \$\{\{ success\(\) && steps.vcpkg-cache.outputs.enabled == 'true' && steps.vcpkg-restore.outputs.cache-hit != 'true' \}\}$/m);
  assert.match(onlyStep(applications, 'name: Save vcpkg package archives\n'), /continue-on-error: true/);
  assert.match(verify, /^        if: steps.vcpkg-restore.outputs.cache-hit != 'true'$/m);
  assert.match(verify, /lookup-only: true\n          fail-on-cache-miss: true/);
  assert.match(verify, /key: \$\{\{ steps.vcpkg-cache.outputs.key \}\}/);
  assert.doesNotMatch(verify, /restore-keys:/);
  assert.match(available, /VCPKG_ARCHIVE_HIT: \$\{\{ steps.vcpkg-restore.outputs.cache-hit \}\}/);
  assert.match(available, /VCPKG_VERIFIED_HIT: \$\{\{ steps.seed-verify.outputs.cache-hit \}\}/);
  assert.match(available, /run: node \.github\/scripts\/vcpkg-seed-check.mjs --available/);
});

test('disabled providers, incomplete keys and absent or empty archives cannot produce a seed', t => {
  const directory = mkdtempSync(join(tmpdir(), 'ark-seed-check-'));
  t.after(() => rmSync(directory, { recursive: true, force: true }));
  const env = { VCPKG_ARCHIVE_ENABLED: 'true', VCPKG_ARCHIVE_DIRECTORY: directory,
    VCPKG_ARCHIVE_KEY: `ark-vcpkg-files-v2-Windows-X64-${'a'.repeat(64)}-${'b'.repeat(64)}` };
  for (const extra of [{ VCPKG_ARCHIVE_ENABLED: 'false' }, { VCPKG_ARCHIVE_ENABLED: '' },
    { VCPKG_ARCHIVE_KEY: '' }, { VCPKG_ARCHIVE_KEY: 'partial' },
    { VCPKG_ARCHIVE_DIRECTORY: '' }, { VCPKG_ARCHIVE_DIRECTORY: join(directory, 'missing') }, {}]) {
    assert.throws(() => requireArchives({ ...env, ...extra }));
  }
  mkdirSync(join(directory, 'not-a-file.zip'));
  assert.throws(() => requireArchives(env));
  mkdirSync(join(directory, 'ab'));
  writeFileSync(join(directory, 'ab', 'package.zip'), 'archive fixture');
  assert.equal(requireArchives(env), 1);
  const script = fileURLToPath(new URL('../../.github/scripts/vcpkg-seed-check.mjs', import.meta.url));
  assert.equal(spawnSync(process.execPath, [script], { env }).status, 0);
  assert.equal(spawnSync(process.execPath, [script], { env: { ...env, VCPKG_ARCHIVE_ENABLED: 'false' } }).status, 1);
});

test('an existing exact hit or a verified saved entry succeeds; failed/warning-only saves fail', () => {
  for (const restored of ['true', 'false', '', undefined]) {
    for (const verified of ['true', 'false', '', undefined]) {
      const env = { VCPKG_ARCHIVE_HIT: restored, VCPKG_VERIFIED_HIT: verified };
      if (restored === 'true' || verified === 'true') assert.doesNotThrow(() => requireAvailableSeed(env));
      else assert.throws(() => requireAvailableSeed(env), /verified exact cache entry/);
    }
  }
  const script = fileURLToPath(new URL('../../.github/scripts/vcpkg-seed-check.mjs', import.meta.url));
  assert.equal(spawnSync(process.execPath, [script, '--available'], { env: {} }).status, 1);
  assert.equal(spawnSync(process.execPath, [script, '--available'], { env: { VCPKG_ARCHIVE_HIT: 'true' } }).status, 0);
});

test('seed diagnostics preserve observable archive/ABI evidence without becoming native acceptance', () => {
  const diagnostic = onlyStep(seed, 'vcpkg-abi-diagnostics.mjs');
  const statistics = onlyStep(seed, 'cache-stats.mjs');
  assert.ok(seed.indexOf(diagnostic) > seed.indexOf('name: Save vcpkg package archives\n'));
  for (const step of [diagnostic, statistics]) {
    assert.match(step, /^        if: \$\{\{ !cancelled\(\) \}\}$/m);
    assert.match(step, /^        continue-on-error: true$/m);
  }
  assert.match(statistics, /VCPKG_ARCHIVE_DIRECTORY: \$\{\{ steps.vcpkg-cache.outputs.directory \}\}/);
  assert.match(statistics, /VCPKG_ARCHIVE_HIT: \$\{\{ steps.vcpkg-restore.outputs.cache-hit \}\}/);
  assert.doesNotMatch(seed, /CI - required|check-core|check-network|check-database/);
});
