// Copyright (c) 2026 OpenArkStudio. SPDX-License-Identifier: Apache-2.0
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { delimiter, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { applicationCommands } from '../../.github/scripts/ci-applications.mjs';

const exporter = fileURLToPath(new URL('../../.github/scripts/windows-runtime.cmake', import.meta.url));
const workflow = readFileSync(new URL('../../.github/workflows/ci.yml', import.meta.url), 'utf8');
const job = name => workflow.split(/(?=^  [a-z][a-z0-9_-]*:$)/m).find(block => block.startsWith(`  ${name}:\n`));
const cmakePath = value => value.replaceAll('\\', '/');

function fixture(t, overrides = {}) {
  const root = mkdtempSync(resolve(tmpdir(), 'ark-windows-runtime-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const build = join(root, 'configured build');
  const installed = join(root, 'actual installed dependencies');
  const triplet = 'x64-windows';
  const debugBin = join(installed, triplet, 'debug', 'bin');
  const releaseBin = join(installed, triplet, 'bin');
  mkdirSync(debugBin, { recursive: true });
  mkdirSync(releaseBin, { recursive: true });
  mkdirSync(build);
  const values = {
    VCPKG_INSTALLED_DIR: cmakePath(installed), VCPKG_TARGET_TRIPLET: triplet,
    VCPKG_APPLOCAL_DEPS: 'OFF', CMAKE_BUILD_TYPE: 'Debug', ...overrides,
  };
  writeFileSync(join(build, 'CMakeCache.txt'), Object.entries(values)
    .filter(([, value]) => value !== undefined).map(([key, value]) => {
      const type = key === 'VCPKG_INSTALLED_DIR' ? 'PATH' : key === 'VCPKG_APPLOCAL_DEPS' ? 'BOOL' : 'STRING';
      return `${key}:${type}=${value}\n`;
    }).join(''));
  const output = join(root, 'github-path');
  writeFileSync(output, 'existing-entry\n');
  const run = (extra = {}) => execFileSync('cmake', [`-DBUILD_DIR=${cmakePath(build)}`, '-P', exporter],
    { env: { ...process.env, GITHUB_PATH: output, ...extra }, stdio: ['ignore', 'pipe', 'pipe'], encoding: 'utf8' });
  return { root, build, debugBin, releaseBin, output, run };
}

test('only Windows Applications disables per-target staging and exports its configured dependency directory before build', () => {
  const applications = job('applications');
  assert.match(applications, /run: node \.github\/scripts\/ci-applications\.mjs configure\n/);
  for (const runner of ['Windows', 'Linux', 'macOS']) {
    const [args] = applicationCommands('configure', { GITHUB_EVENT_NAME: 'merge_group', RUNNER_OS: runner, VCPKG_ROOT: '/vcpkg' });
    assert.equal(args.includes('-DVCPKG_APPLOCAL_DEPS=OFF'), runner === 'Windows', runner);
  }
  const runtime = applications.split(/(?=^      - )/m).find(step => step.includes('Set Windows dependency runtime PATH'));
  assert.match(runtime, /^        if: runner.os == 'Windows'$/m);
  assert.match(runtime, /run: cmake -DBUILD_DIR=ci-build -P .github\/scripts\/windows-runtime.cmake/);
  assert.doesNotMatch(runtime, /continue-on-error:|always\(\)|\|\| true/);
  assert.ok(applications.indexOf('Configure applications') < applications.indexOf(runtime));
  const build = applications.indexOf('run: node .github/scripts/ci-applications.mjs build\n');
  const check = applications.indexOf('run: node .github/scripts/ci-applications.mjs check\n');
  assert.ok(applications.indexOf(runtime) < build);
  assert.ok(build < check);
  assert.ok(applicationCommands('check', { GITHUB_EVENT_NAME: 'merge_group', RUNNER_OS: 'Windows' })
    .some(command => command[3] === 'check-core'));
  assert.doesNotMatch(job('network') + job('database'), /VCPKG_APPLOCAL_DEPS|windows-runtime.cmake/);
  const core = readFileSync(new URL('../core/CMakeLists.txt', import.meta.url), 'utf8');
  for (const check of ['check-app-business-runtime', 'check-app-startup-failures', 'check-app-manifest-failures']) {
    assert.ok(core.includes(`add_dependencies(check-core ${check})`));
  }
});

test('actual CMake cache selection exports only Debug or release-family runtime directories and preserves existing output', t => {
  for (const configuration of ['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel']) {
    const f = fixture(t, { CMAKE_BUILD_TYPE: configuration });
    f.run();
    const expected = configuration === 'Debug' ? f.debugBin : f.releaseBin;
    assert.equal(readFileSync(f.output, 'utf8'), `existing-entry\n${expected}\n`);
  }
});

test('missing inconsistent or unusable CMake runtime inputs fail without exporting a fallback', t => {
  for (const overrides of [
    { VCPKG_INSTALLED_DIR: undefined }, { VCPKG_INSTALLED_DIR: 'relative/install' },
    { VCPKG_TARGET_TRIPLET: undefined }, { VCPKG_TARGET_TRIPLET: 'x64-linux' },
    { VCPKG_TARGET_TRIPLET: '../other' }, { VCPKG_APPLOCAL_DEPS: undefined },
    { VCPKG_APPLOCAL_DEPS: 'ON' }, { CMAKE_BUILD_TYPE: undefined }, { CMAKE_BUILD_TYPE: 'Unknown' },
  ]) {
    const f = fixture(t, overrides);
    assert.throws(() => f.run(), `reject ${JSON.stringify(overrides)}`);
    assert.equal(readFileSync(f.output, 'utf8'), 'existing-entry\n');
  }
  const f = fixture(t);
  rmSync(f.debugBin, { recursive: true });
  assert.throws(() => f.run(), /dependency runtime directory is missing/);
  assert.equal(readFileSync(f.output, 'utf8'), 'existing-entry\n', 'do not fall back to Release');
  rmSync(join(f.build, 'CMakeCache.txt'));
  assert.throws(() => f.run(), /configured BUILD_DIR is required/);
});

test('missing or unusable GitHub output channels fail explicitly', t => {
  const f = fixture(t);
  assert.throws(() => f.run({ GITHUB_PATH: '' }), /GITHUB_PATH file is required/);
  assert.throws(() => f.run({ GITHUB_PATH: join(f.root, 'missing-channel') }), /GITHUB_PATH file is required/);
  assert.throws(() => f.run({ GITHUB_PATH: f.root }));
  assert.equal(readFileSync(f.output, 'utf8'), 'existing-entry\n');
});

test('CMake build targets and their subprocesses inherit the exported directory and propagate failures', t => {
  const f = fixture(t);
  f.run();
  const exported = readFileSync(f.output, 'utf8').trimEnd().split('\n').at(-1);
  const source = join(f.root, 'runtime target');
  const build = join(f.root, 'runtime target build');
  mkdirSync(source);
  writeFileSync(join(source, 'probe.mjs'), `import assert from 'node:assert/strict';
assert.equal(process.env.PATH.split(${JSON.stringify(delimiter)})[0], ${JSON.stringify(f.debugBin)});
assert.ok(!process.env.PATH.split(${JSON.stringify(delimiter)}).includes(${JSON.stringify(f.releaseBin)}));
process.exit(Number(process.env.ARK_RUNTIME_TEST_EXIT ?? 0));\n`);
  writeFileSync(join(source, 'child.cmake'), `execute_process(COMMAND "${cmakePath(process.execPath)}" "${cmakePath(join(source, 'probe.mjs'))}" RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
  message(FATAL_ERROR "Runtime probe failed: \${result}")
endif()\n`);
  writeFileSync(join(source, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.21)
project(runtime_fixture NONE)
add_custom_target(check-core COMMAND "\${CMAKE_COMMAND}" -P "\${CMAKE_CURRENT_SOURCE_DIR}/child.cmake" VERBATIM)\n`);
  execFileSync('cmake', ['-S', source, '-B', build], { stdio: ['ignore', 'pipe', 'pipe'] });
  const env = { ...process.env, PATH: exported + delimiter + process.env.PATH };
  const run = exit => execFileSync('cmake', ['--build', build, '--target', 'check-core'],
    { env: { ...env, ARK_RUNTIME_TEST_EXIT: String(exit) }, stdio: ['ignore', 'pipe', 'pipe'] });
  run(0);
  assert.throws(() => run(7), /Runtime probe failed: 7/);
});
