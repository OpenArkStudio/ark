import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import test from 'node:test';

const workflow = readFileSync(new URL('../../.github/workflows/ci.yml', import.meta.url), 'utf8').replaceAll('\r\n', '\n');
const profiles = [
  ['network', 'Configure network regressions', 'ci-build', ['check-network']],
  ['applications', 'Configure applications', 'ci-build', ['check-core']],
  ['database', 'Configure native database development tests', 'ci-database',
    ['check-database', 'check-database-benchmark']],
];

// Read this workflow's job/step indentation; this is not a general YAML parser.
function jobSteps(name) {
  const jobs = workflow.split(/(?=^  [a-z][a-z0-9_-]*:$)/m);
  const job = jobs.find(block => block.startsWith(`  ${name}:\n`));
  assert.ok(job, `missing job ${name}`);
  const [settings, ...steps] = job.split(/(?=^      - )/m);
  assert.doesNotMatch(settings, /^    continue-on-error:/m, `${name} must remain fatal`);
  assert.ok(steps.length > 0, `${name} must contain steps`);
  return steps;
}

function onlyStep(steps, predicate, description) {
  const found = steps.map((step, index) => ({ step, index })).filter(({ step }) => predicate(step));
  assert.equal(found.length, 1, `expected exactly one ${description}`);
  return found[0];
}

// Evaluate the checked-in save guard, including its explicit status function;
// otherwise Actions would add success() and skip this step after a failure.
function compilerSaveRuns(step, { previous = ['success'], outcome = 'success', enabled = 'true', cacheHit = 'false' } = {}) {
  const expression = step.match(/^        if: \$\{\{ (.+) \}\}$/m)?.[1];
  assert.ok(expression, 'compiler save must have an explicit condition');
  assert.match(expression, /\bcancelled\(\)/, 'save must override implicit success without running after cancellation');
  const javascript = expression
    .replace(/steps\.([a-z][a-z0-9_-]*)/g, (_, id) => `steps[${JSON.stringify(id)}]`)
    .replace(/\.cache-hit\b/g, "['cache-hit']");
  return runInNewContext(javascript, {
    cancelled: () => previous.includes('cancelled'),
    success: () => !previous.includes('failure') && !previous.includes('cancelled'),
    steps: {
      'compiler-start': { outcome, outputs: { enabled } },
      'compiler-restore': { outputs: { 'cache-hit': cacheHit } },
    },
  });
}

for (const [name, configureName, buildDirectory, checks] of profiles) {
  test(`${name} saves dependency archives immediately after successful configure`, () => {
    const steps = jobSteps(name);
    const configure = onlyStep(steps, step => step.startsWith(`      - name: ${configureName}\n`), 'configure step');
    const save = onlyStep(steps, step => step.includes('name: Save vcpkg package archives\n'), 'archive save');
    const build = onlyStep(steps, step => step.includes(`run: cmake --build ${buildDirectory} --parallel `), 'source build');
    assert.equal(save.index, configure.index + 1, 'save only after configure has completed');
    assert.ok(save.index < build.index, 'save dependencies before compiling project sources');
    assert.match(save.step, /^        if: \$\{\{ success\(\) && steps\.vcpkg-cache\.outputs\.enabled == 'true' && steps\.vcpkg-restore\.outputs\.cache-hit != 'true' \}\}$/m);
    assert.match(save.step, /^        continue-on-error: true$/m);
    assert.match(save.step, /^          path: \$\{\{ steps\.vcpkg-cache\.outputs\.directory \}\}$/m);
    assert.match(save.step, /^          key: \$\{\{ steps\.vcpkg-cache\.outputs\.key \}\}$/m);
  });

  test(`${name} restores exact archives with one compatible prefix and saves only the exact key`, () => {
    const steps = jobSteps(name);
    const prepare = onlyStep(steps, step => step.includes('id: vcpkg-cache\n'), 'cache identity');
    const restore = onlyStep(steps, step => step.includes('id: vcpkg-restore\n'), 'archive restore');
    const save = onlyStep(steps, step => step.includes('name: Save vcpkg package archives\n'), 'archive save');
    const toolchain = onlyStep(steps, step => step.includes('uses: lukka/run-vcpkg@v11\n'), 'vcpkg action');
    assert.ok(prepare.index < restore.index && restore.index < toolchain.index);
    assert.match(restore.step, /^        if: steps\.vcpkg-cache\.outputs\.enabled == 'true'$/m);
    assert.match(restore.step, /^        continue-on-error: true$/m);
    assert.match(restore.step, /^          path: \$\{\{ steps\.vcpkg-cache\.outputs\.directory \}\}$/m);
    assert.match(restore.step, /^          key: \$\{\{ steps\.vcpkg-cache\.outputs\.key \}\}$/m);
    assert.match(restore.step, /^          restore-keys: \$\{\{ steps\.vcpkg-cache\.outputs\.restore-key \}\}$/m);
    assert.equal((restore.step.match(/restore-keys:/g) ?? []).length, 1);
    assert.doesNotMatch(save.step, /restore-keys:/);
    assert.equal(steps.filter(step => step.includes('uses: actions/cache')).length, 4);
    assert.match(save.step, /uses: actions\/cache\/save@v4/);
    assert.match(restore.step, /uses: actions\/cache\/restore@v4/);
    for (const { step } of [prepare, toolchain]) {
      assert.doesNotMatch(step, /VCPKG_DISABLE_COMPILER_TRACKING|VCPKG_ENV_PASSTHROUGH_UNTRACKED|--editable/);
    }
  });

  test(`${name} still requires configure, compilation and every regression command`, () => {
    const steps = jobSteps(name);
    const configure = onlyStep(steps, step => step.startsWith(`      - name: ${configureName}\n`), 'configure step');
    const build = onlyStep(steps, step => step.includes(`run: cmake --build ${buildDirectory} --parallel `), 'source build');
    const required = [configure, build, ...checks.map(target => onlyStep(steps,
      step => step.includes(`run: cmake --build ${buildDirectory} --target ${target}\n`), target))];
    assert.match(configure.step, /^          cmake -S \. -B /m);
    for (const { step } of required) {
      assert.doesNotMatch(step, /^        continue-on-error:/m, 'configure/build/test failure must fail the job');
      assert.doesNotMatch(step, /^        if:/m, 'configure/build/tests must retain their default success condition');
    }
    for (const check of required.slice(2)) {
      assert.ok(build.index < check.index, 'compile before running regressions');
    }
  });
}

for (const [name, configureName] of profiles) {
  test(`${name} restores optional pinned compiler objects before starting the launcher`, () => {
    const steps = jobSteps(name);
    const find = id => onlyStep(steps, step => step.includes(`id: ${id}\n`), id);
    const setup = find('sccache-setup');
    const prepare = find('compiler-cache');
    const restore = find('compiler-restore');
    const start = find('compiler-start');
    const configure = onlyStep(steps, step => step.startsWith(`      - name: ${configureName}\n`), 'configure step');
    const save = onlyStep(steps, step => step.includes('name: Save compiler cache\n'), 'compiler save');
    assert.ok(setup.index < prepare.index && prepare.index < restore.index && restore.index < start.index && start.index < configure.index);
    assert.match(setup.step, /uses: Mozilla-Actions\/sccache-action@v0\.0\.11/);
    assert.match(setup.step, /version: v0\.16\.0/);
    assert.match(setup.step, /disable_annotations: true/);
    assert.match(setup.step, /if: runner\.os != 'Windows'/);
    for (const { step } of [setup, restore, save]) assert.match(step, /continue-on-error: true/);
    for (const { step } of [prepare, start]) {
      assert.match(step, /COMPILER_CACHE_IDENTITY: \$\{\{ steps\.vcpkg-cache\.outputs\.key \}\}/);
      assert.match(step, /SCCACHE_SETUP_OUTCOME: \$\{\{ steps\.sccache-setup\.outcome \}\}/);
    }
    assert.match(prepare.step, /compiler-cache\.mjs --prepare/);
    assert.match(start.step, /SCCACHE_RESTORE_OUTCOME: \$\{\{ steps\.compiler-restore\.outcome \}\}/);
    assert.match(restore.step, /key: \$\{\{ steps\.compiler-cache\.outputs\.archive_key \}\}/);
    assert.match(restore.step, /restore-keys: \$\{\{ steps\.compiler-cache\.outputs\.restore_keys \}\}/);
    for (const language of ['C', 'CXX']) {
      assert.ok(configure.step.includes(`"-DCMAKE_${language}_COMPILER_LAUNCHER=$env:ARK_COMPILER_LAUNCHER"`));
    }
    assert.match(save.step, /if: \$\{\{ !cancelled\(\) && steps\.compiler-start\.outcome == 'success' && steps\.compiler-start\.outputs\.enabled == 'true' && steps\.compiler-restore\.outputs\.cache-hit != 'true' \}\}/);
    assert.match(save.step, /path: \$\{\{ steps\.compiler-cache\.outputs\.directory \}\}/);
    assert.match(save.step, /key: \$\{\{ steps\.compiler-cache\.outputs\.archive_key \}\}/);
    assert.equal(save.index, steps.length - 1, 'save only after all source builds and checks');
    assert.doesNotMatch(configure.step, /CMAKE_(?:C|CXX)_FLAGS=.*\/(?:Zi|Z7)/);
  });

  test(`${name} saves compiler objects after partial build or regression failures`, () => {
    const { step } = onlyStep(jobSteps(name), step => step.includes('name: Save compiler cache\n'), 'compiler save');
    for (const previous of [
      ['success'],
      ['success', 'failure', 'skipped'], // Some objects compiled before the build failed.
      ['success', 'success', 'failure'], // Compilation succeeded, a regression failed.
    ]) {
      assert.equal(compilerSaveRuns(step, { previous }), true, JSON.stringify(previous));
    }
  });

  test(`${name} never saves cancelled, disabled, uninitialized or exact-hit compiler caches`, () => {
    const { step } = onlyStep(jobSteps(name), step => step.includes('name: Save compiler cache\n'), 'compiler save');
    for (const scenario of [
      { previous: ['success', 'cancelled'] },
      { previous: ['failure', 'cancelled'] },
      { enabled: 'false' },
      { enabled: '' },
      ...['failure', 'cancelled', 'skipped', ''].map(outcome => ({ outcome })),
      { cacheHit: 'true' },
      { previous: ['success', 'failure'], cacheHit: 'true' },
    ]) {
      assert.equal(compilerSaveRuns(step, scenario), false, JSON.stringify(scenario));
    }
    assert.equal(compilerSaveRuns(step, { cacheHit: '' }), true, 'a successful restore miss may omit cache-hit');
  });
}
