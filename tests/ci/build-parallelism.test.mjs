import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { buildParallelismPlan, detectBuildResources, exportBuildParallelism } from '../../.github/scripts/build-parallelism.mjs';

const gibibyte = 1024 ** 3;

function fixture(t) {
  const root = mkdtempSync(join(tmpdir(), 'ark-build-pools-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  return root;
}

test('CPU and memory jointly bound compilation while small runners retain two jobs', () => {
  for (const [cpuCount, gib, buildJobs, linkJobs] of [
    [4, 16, 4, 2], [64, 256, 4, 2], [3, 7, 2, 1], [8, 8, 2, 1],
    [8, 10, 3, 1], [8, 4, 1, 1], [1, 16, 1, 1], [4, 0.5, 1, 1],
  ]) {
    const plan = buildParallelismPlan({ cpuCount, memoryBytes: gib * gibibyte });
    assert.equal(plan.buildJobs, buildJobs, `${cpuCount} CPUs / ${gib} GiB`);
    assert.equal(plan.linkJobs, linkJobs, `${cpuCount} CPUs / ${gib} GiB`);
    assert.equal(plan.jobPools, `ark_compile=${buildJobs};ark_link=${linkJobs}`);
  }
});

test('missing, malformed and failed resource readings fall back conservatively', () => {
  for (const invalid of [undefined, null, 0, -1, 1.5, NaN, Infinity, '16', {}, Number.MAX_SAFE_INTEGER + 1]) {
    const missingCpu = buildParallelismPlan({ cpuCount: invalid, memoryBytes: 16 * gibibyte });
    assert.equal(missingCpu.buildJobs, 2);
    assert.equal(missingCpu.linkJobs, 1);
    const plan = buildParallelismPlan({ cpuCount: 16, memoryBytes: invalid });
    assert.equal(plan.buildJobs, 2);
    assert.equal(plan.linkJobs, 1);
  }
  assert.equal(buildParallelismPlan({}).buildJobs, 2);
  assert.equal(buildParallelismPlan({ memoryBytes: 4 * gibibyte }).buildJobs, 1);
  const fail = () => { throw new Error('resource unavailable'); };
  assert.deepEqual(detectBuildResources({ readCpuCount: fail, readTotalMemory: fail, readConstrainedMemory: fail }),
    { cpuCount: null, memoryBytes: null });
});

test('detection respects process memory limits and treats an unconstrained zero as unknown', () => {
  const readCpuCount = () => 8;
  const readTotalMemory = () => 64 * gibibyte;
  const limited = detectBuildResources({ readCpuCount, readTotalMemory, readConstrainedMemory: () => 7 * gibibyte });
  assert.deepEqual(limited, { cpuCount: 8, memoryBytes: 7 * gibibyte });
  assert.equal(buildParallelismPlan(limited).buildJobs, 2);
  for (const limit of [0, -1, NaN, Infinity, undefined]) {
    assert.equal(detectBuildResources({ readCpuCount, readTotalMemory, readConstrainedMemory: () => limit }).memoryBytes, 64 * gibibyte);
  }
  assert.equal(detectBuildResources({ readCpuCount, readTotalMemory: () => 8 * gibibyte,
    readConstrainedMemory: () => 64 * gibibyte }).memoryBytes, 8 * gibibyte);
});

test('overrides only reduce resource budgets and malformed overrides use conservative limits', () => {
  const resources = { cpuCount: 16, memoryBytes: 64 * gibibyte };
  for (const value of ['1', '2', '3', '4']) {
    assert.equal(buildParallelismPlan({ ...resources, buildJobsOverride: value }).buildJobs, Number(value));
  }
  assert.equal(buildParallelismPlan({ cpuCount: 2, memoryBytes: 7 * gibibyte, buildJobsOverride: '4' }).buildJobs, 2);
  assert.equal(buildParallelismPlan({ ...resources, buildJobsOverride: '1', linkJobsOverride: '2' }).linkJobs, 1);
  assert.equal(buildParallelismPlan({ ...resources, buildJobsOverride: '2', linkJobsOverride: '2' }).linkJobs, 1);
  assert.equal(buildParallelismPlan({ ...resources, linkJobsOverride: '1' }).linkJobs, 1);
  assert.equal(buildParallelismPlan({ ...resources, linkJobsOverride: '2' }).linkJobs, 2);
  assert.equal(buildParallelismPlan({ ...resources, buildJobsOverride: '' }).buildJobs, 4);
  for (const value of ['0', '-1', '5', 'Infinity', '2.5', '02', ' 2 ', '2\nINJECTED=value', 4, null]) {
    assert.equal(buildParallelismPlan({ ...resources, buildJobsOverride: value }).buildJobs, 2, String(value));
    assert.equal(buildParallelismPlan({ ...resources, linkJobsOverride: value }).linkJobs, 1, String(value));
  }
});

test('GitHub channels export only validated integers and fixed Ninja pool names', t => {
  const root = fixture(t);
  const env = { GITHUB_ENV: join(root, 'env'), GITHUB_OUTPUT: join(root, 'output'),
    ARK_BUILD_JOBS_OVERRIDE: '4\nINJECTED=value', ARK_LINK_JOBS_OVERRIDE: '2\nINJECTED=value' };
  const plan = exportBuildParallelism({ env, resources: { cpuCount: 4, memoryBytes: 16 * gibibyte } });
  assert.equal(plan.buildJobs, 2);
  assert.equal(plan.linkJobs, 1);
  assert.equal(readFileSync(env.GITHUB_ENV, 'utf8'), 'ARK_BUILD_JOBS=2\nARK_LINK_JOBS=1\nARK_CMAKE_JOB_POOLS=ark_compile=2;ark_link=1\n');
  assert.equal(readFileSync(env.GITHUB_OUTPUT, 'utf8'),
    `build_jobs=2\nlink_jobs=1\njob_pools=ark_compile=2;ark_link=1\ncpu_count=4\nmemory_bytes=${16 * gibibyte}\n`);
  for (const key of ['GITHUB_ENV', 'GITHUB_OUTPUT']) {
    assert.throws(() => exportBuildParallelism({ env: { ...env, [key]: '' }, resources: {} }), /file-command/);
  }
});

test('Ninja applies compile and link pools to real C++ builds and preserves compiler failures', { skip: process.platform === 'win32' }, t => {
  const root = fixture(t);
  const launcher = join(root, 'record jobs.mjs');
  const trace = join(root, 'trace');
  writeFileSync(launcher, `import { spawnSync } from 'node:child_process';
import { appendFileSync } from 'node:fs';
const [kind, command, ...args] = process.argv.slice(2);
const record = phase => appendFileSync(${JSON.stringify(trace)}, JSON.stringify({ kind, phase }) + '\\n');
record('start');
Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 100);
const result = spawnSync(command, args, { stdio: 'inherit' });
record('end');
process.exit(result.status ?? 1);
`);
  writeFileSync(join(root, 'CMakeLists.txt'), `cmake_minimum_required(VERSION 3.21)
project(build_pools LANGUAGES CXX)
foreach(n RANGE 1 4)
  add_executable(check_\${n} main.cpp)
endforeach()
`);
  writeFileSync(join(root, 'main.cpp'), 'int main() { return 0; }\n');
  const run = args => spawnSync('cmake', args, { encoding: 'utf8', timeout: 30_000 });
  for (const gib of [8, 16]) {
    const plan = buildParallelismPlan({ cpuCount: 4, memoryBytes: gib * gibibyte });
    const build = join(root, `build-${gib}`);
    const configured = run(['-S', root, '-B', build, '-G', 'Ninja',
      `-DCMAKE_JOB_POOLS=${plan.jobPools}`, '-DCMAKE_JOB_POOL_COMPILE=ark_compile', '-DCMAKE_JOB_POOL_LINK=ark_link',
      `-DCMAKE_CXX_COMPILER_LAUNCHER=${process.execPath};${launcher};compile`,
      `-DCMAKE_CXX_LINKER_LAUNCHER=${process.execPath};${launcher};link`]);
    assert.equal(configured.status, 0, configured.stdout + configured.stderr);
    const ninja = readFileSync(join(build, 'build.ninja'), 'utf8');
    const rules = readFileSync(join(build, 'CMakeFiles', 'rules.ninja'), 'utf8');
    assert.match(rules, new RegExp(`pool ark_compile\\n  depth = ${plan.buildJobs}\\n`));
    assert.match(rules, new RegExp(`pool ark_link\\n  depth = ${plan.linkJobs}\\n`));
    assert.equal((ninja.match(/pool = ark_compile/g) ?? []).length, 4);
    assert.equal((ninja.match(/pool = ark_link/g) ?? []).length, 4);
    writeFileSync(trace, '');
    const compiled = run(['--build', build, '--parallel', String(plan.buildJobs)]);
    assert.equal(compiled.status, 0, compiled.stdout + compiled.stderr);
    const active = { compile: 0, link: 0 };
    const peaks = { compile: 0, link: 0 };
    const starts = { compile: 0, link: 0 };
    for (const line of readFileSync(trace, 'utf8').trim().split('\n')) {
      const { kind, phase } = JSON.parse(line);
      active[kind] += phase === 'start' ? 1 : -1;
      if (phase === 'start') starts[kind]++;
      peaks[kind] = Math.max(peaks[kind], active[kind]);
      assert.ok(active.compile + active.link <= plan.buildJobs, 'overall build parallelism is bounded');
    }
    assert.deepEqual(starts, { compile: 4, link: 4 });
    assert.deepEqual(active, { compile: 0, link: 0 });
    assert.ok(peaks.compile >= 2 && peaks.compile <= plan.buildJobs);
    assert.ok(peaks.link >= 1 && peaks.link <= plan.linkJobs);
    const executable = spawnSync(join(build, 'check_1'), [], { encoding: 'utf8', timeout: 5_000 });
    assert.equal(executable.status, 0, executable.stdout + executable.stderr);
  }
  writeFileSync(join(root, 'main.cpp'), '#error intentional compiler failure\n');
  const failed = run(['--build', join(root, 'build-16'), '--parallel', '4']);
  assert.notEqual(failed.status, 0);
  assert.match(failed.stdout + failed.stderr, /intentional compiler failure/);
});
