import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { applicationCommands, applicationPlan, runApplications } from '../../.github/scripts/ci-applications.mjs';
import { applicationConfigureFlags, fullApplicationBuildPlan } from '../../.github/scripts/application-build-plan.mjs';
import { compilerSeedPlan } from '../../.github/scripts/compiler-seed-plan.mjs';

const full = fullApplicationBuildPlan();
const narrow = { ...full, mode: 'targets', buildAll: false, reason: 'reviewed-inputs',
  checkTargets: ['check-app-shutdown-deadlines'] };
const source = { GITHUB_EVENT_NAME: 'pull_request', ARK_APPLICATION_VALIDATION_STAGE: 'modules',
  ARK_APPLICATION_SCOPE_RESULT: 'success',
  ARK_APPLICATION_FILES_KNOWN: 'true', ARK_APPLICATION_PLAN: JSON.stringify(narrow),
  VCPKG_ROOT: '/tool cache/vcpkg', ARK_COMPILER_LAUNCHER: '/tool cache/sccache',
  ARK_BUILD_JOBS: '4', ARK_CMAKE_JOB_POOLS: 'ark_compile=4;ark_link=1' };

test('reviewed source plans build check-target dependency closures instead of ALL', () => {
  assert.equal(applicationPlan(source).mode, 'targets');
  assert.deepEqual(applicationCommands('build', source), []);
  assert.deepEqual(applicationCommands('check', source), [
    ['--build', 'ci-build', '--target', 'check-app-shutdown-deadlines', '--parallel', '4'],
  ]);
});

test('queue, manual and missing or malformed scope always restore full application coverage', () => {
  const corruptions = [
    { GITHUB_EVENT_NAME: 'merge_group' }, { GITHUB_EVENT_NAME: 'workflow_dispatch' },
    ...['failure', 'cancelled', 'skipped', undefined].map(result => ({ ARK_APPLICATION_SCOPE_RESULT: result })),
    { ARK_APPLICATION_VALIDATION_STAGE: '' }, { ARK_APPLICATION_FILES_KNOWN: '' },
    { ARK_APPLICATION_PLAN: '' }, { ARK_APPLICATION_PLAN: '{}' }, { ARK_APPLICATION_PLAN: 'null' },
    ...[
      { mode: 'skip' }, { version: 2 }, { buildAll: true }, { checkTargets: [] },
      { checkTargets: ['install'] }, { checkTargets: ['--clean-first'] },
      { checkTargets: [narrow.checkTargets[0], narrow.checkTargets[0]] },
      { configureFlags: [...applicationConfigureFlags, '-DARK_BUILD_CORE_TESTS=OFF'] },
    ].map(change => ({ ARK_APPLICATION_PLAN: JSON.stringify({ ...narrow, ...change }) })),
  ];
  for (const change of corruptions) {
    const env = { ...source, ...change };
    assert.equal(applicationPlan(env).mode, 'full', JSON.stringify(change));
    assert.equal(applicationCommands('build', env).length, 1);
    assert.deepEqual(applicationCommands('check', env).map(args => args[3]),
      ['check-core', 'check-entity-benchmark-smoke']);
  }
});

test('configuration preserves complete flags, Windows debug settings and literal paths', () => {
  const [args] = applicationCommands('configure', { ...source, RUNNER_OS: 'Windows' });
  assert.ok(args.includes('-DCMAKE_CXX_COMPILER_LAUNCHER=/tool cache/sccache'));
  assert.ok(args.includes('-DCMAKE_JOB_POOLS=ark_compile=4;ark_link=1'));
  assert.ok(args.includes('-DCMAKE_JOB_POOL_LINK=ark_link'));
  assert.ok(args.includes('-DCMAKE_BUILD_TYPE=Debug'));
  assert.ok(args.includes('-DVCPKG_APPLOCAL_DEPS=OFF'));
  assert.deepEqual(args.slice(-applicationConfigureFlags.length), [...applicationConfigureFlags]);
  assert.ok(args.includes('-DARK_BUILD_PLUGIN_CONSUL=ON'));
  assert.ok(args.includes('-DARK_BUILD_PLUGIN_ETCD_CLIENT=ON'));
  assert.ok(!args.some(arg => /\/(?:Zi|Z7)/.test(arg)));
});

test('a failed CMake command ends the phase without a shell, retry or later check', () => {
  const calls = [];
  const env = { ...source, GITHUB_EVENT_NAME: 'merge_group' };
  assert.equal(runApplications('check', env, (command, args, options) => {
    calls.push({ command, args, options });
    return { status: 27 };
  }), 27);
  assert.equal(calls.length, 1);
  assert.equal(calls[0].command, 'cmake');
  assert.equal(calls[0].options.shell, false);
  assert.equal(runApplications('build', env, () => ({ status: null })), 1);
  assert.throws(() => runApplications('build', env, () => ({ error: new Error('cannot spawn') })), /cannot spawn/);
  assert.throws(() => applicationCommands('unexpected', source), /Unknown applications phase/);
});

test('cache warmup only accepts current trusted main and skips documentation or superseded pushes', () => {
  const env = { GITHUB_EVENT_NAME: 'push', GITHUB_REF: 'refs/heads/main', GITHUB_SHA: 'a'.repeat(40) };
  assert.equal(compilerSeedPlan({ env, latestMain: env.GITHUB_SHA, files: ['src/core/module.hpp'] }).build, true);
  assert.equal(compilerSeedPlan({ env, latestMain: env.GITHUB_SHA, files: ['examples/guest_room/README.md'] }).build, false);
  assert.equal(compilerSeedPlan({ env, latestMain: 'b'.repeat(40), files: ['src/core/module.hpp'] }).build, false);
  assert.equal(compilerSeedPlan({ env }).build, true);
  assert.equal(compilerSeedPlan({ env: { ...env, GITHUB_EVENT_NAME: 'pull_request' } }).build, false);
  assert.equal(compilerSeedPlan({ env: { ...env, GITHUB_REF: 'refs/pull/1/merge' } }).build, false);
  assert.equal(compilerSeedPlan({ env: { ...env, GITHUB_EVENT_NAME: 'workflow_dispatch' } }).build, true);
});

test('real CMake target closures omit unrelated sources while full acceptance still builds them', t => {
  const root = mkdtempSync(join(tmpdir(), 'ark applications plan '));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const toolchain = join(root, 'tool cache', 'scripts', 'buildsystems');
  mkdirSync(toolchain, { recursive: true });
  writeFileSync(join(toolchain, 'vcpkg.cmake'), '# Isolated CMake fixture: no package installation.\n');
  writeFileSync(join(root, 'CMakeLists.txt'), [
    'cmake_minimum_required(VERSION 3.21)', 'project(application_plan LANGUAGES CXX)',
    'add_executable(required required.cpp)', 'add_library(unrelated STATIC broken.cpp)',
    'add_custom_target(check-app-shutdown-deadlines COMMAND $<TARGET_FILE:required> DEPENDS required)',
  ].join('\n') + '\n');
  writeFileSync(join(root, 'required.cpp'), 'int main() { return 0; }\n');
  writeFileSync(join(root, 'broken.cpp'), '#error unrelated input must still fail full acceptance\n');
  const env = { ...process.env, ...source, VCPKG_ROOT: join(root, 'tool cache'),
    ARK_COMPILER_LAUNCHER: '', ARK_BUILD_JOBS: '2' };
  let diagnostic = '';
  const run = (command, args, options) => {
    const result = spawnSync(command, args, { ...options, cwd: root, stdio: 'pipe', encoding: 'utf8', timeout: 30_000 });
    diagnostic = (result.stdout ?? '') + (result.stderr ?? '');
    return result;
  };
  assert.equal(runApplications('configure', env, run), 0, diagnostic);
  assert.equal(runApplications('build', env, run), 0, diagnostic);
  assert.equal(runApplications('check', env, run), 0, diagnostic);
  assert.notEqual(runApplications('build', { ...env, GITHUB_EVENT_NAME: 'merge_group' }, run), 0);
  assert.match(diagnostic, /unrelated input must still fail full acceptance/);
});
