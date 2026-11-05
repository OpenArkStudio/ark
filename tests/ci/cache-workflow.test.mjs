import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
import test from 'node:test';
import { applicationCommands, runApplications } from '../../.github/scripts/ci-applications.mjs';
import { selectScope } from '../../.github/scripts/ci-policy.mjs';

const workflow = readFileSync(new URL('../../.github/workflows/ci.yml', import.meta.url), 'utf8').replaceAll('\r\n', '\n');
const compilerSeed = readFileSync(new URL('../../.github/workflows/compiler-cache-seed.yml', import.meta.url), 'utf8').replaceAll('\r\n', '\n');
const profiles = [
  ['network', 'Configure network regressions', 'ci-build', ['check-network']],
  ['applications', 'Configure applications', 'ci-build', ['check-core', 'check-entity-benchmark-smoke']],
  ['database', 'Configure native database development tests', 'ci-database',
    ['check-database', 'check-database-benchmark']],
];

// Read this workflow's job/step indentation; this is not a general YAML parser.
function jobSteps(name, source = workflow) {
  const jobs = source.split(/(?=^  [a-z][a-z0-9_-]*:$)/m);
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

function sourceBuild(steps, name, buildDirectory) {
  return onlyStep(steps, step => name === 'applications'
    ? step.includes('run: node .github/scripts/ci-applications.mjs build\n')
    : step.includes(`run: cmake --build ${buildDirectory} --parallel `), 'source build');
}

function conditionExpression(step) {
  const raw = step.match(/^        if: (.+)$/m)?.[1];
  const expression = raw?.startsWith('${{') ? raw.match(/^\$\{\{ (.+) \}\}$/)?.[1] : raw;
  assert.ok(expression, 'cache save must have an explicit condition');
  return expression.replace(/steps\.([a-z][a-z0-9_-]*)/g, (_, id) => `steps[${JSON.stringify(id)}]`)
    .replace(/\.cache-hit\b/g, "['cache-hit']");
}

// Evaluate the checked-in save guard, including its explicit status function;
// otherwise Actions would add success() and skip this step after a failure.
function compilerSaveRuns(step, { previous = ['success'], outcome = 'success', enabled = 'true', cacheHit = 'false',
  saveEnabled = 'true', eventName = 'pull_request', ref = 'refs/pull/1/merge' } = {}) {
  const expression = conditionExpression(step);
  assert.match(expression, /\bcancelled\(\)/, 'save must override implicit success without running after cancellation');
  return runInNewContext(expression, {
    cancelled: () => previous.includes('cancelled'),
    success: () => !previous.includes('failure') && !previous.includes('cancelled'),
    github: { event_name: eventName, ref },
    steps: {
      'compiler-start': { outcome, outputs: { enabled, save_enabled: saveEnabled } },
      'compiler-restore': { outputs: { 'cache-hit': cacheHit } },
    },
  });
}

for (const [name, configureName, buildDirectory, checks] of profiles) {
  test(`${name} saves dependency archives immediately after successful configure`, () => {
    const steps = jobSteps(name);
    const configure = onlyStep(steps, step => step.startsWith(`      - name: ${configureName}\n`), 'configure step');
    const save = onlyStep(steps, step => step.includes('name: Save vcpkg package archives\n'), 'archive save');
    const build = sourceBuild(steps, name, buildDirectory);
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
    const build = sourceBuild(steps, name, buildDirectory);
    const regressions = name === 'applications'
      ? [onlyStep(steps, step => step.includes('run: node .github/scripts/ci-applications.mjs check\n'), 'application checks')]
      : checks.map(target => onlyStep(steps, step => step.includes(`run: cmake --build ${buildDirectory} --target ${target}\n`), target));
    const required = [configure, build, ...regressions];
    if (name === 'applications') {
      assert.match(configure.step, /^        run: node \.github\/scripts\/ci-applications\.mjs configure$/m);
      assert.deepEqual(applicationCommands('check', { GITHUB_EVENT_NAME: 'merge_group' }).map(args => args[3]), checks);
    } else {
      assert.match(configure.step, /^          cmake -S \. -B /m);
    }
    for (const { step } of required) {
      assert.doesNotMatch(step, /^        continue-on-error:/m, 'configure/build/test failure must fail the job');
      assert.doesNotMatch(step, /^        if:/m, 'configure/build/tests must retain their default success condition');
    }
    for (const check of required.slice(2)) {
      assert.ok(build.index < check.index, 'compile before running regressions');
    }
  });

  test(`${name} applies detected compile and link budgets before configuring Ninja`, () => {
    const steps = jobSteps(name);
    const resources = onlyStep(steps, step => step.includes('id: build-parallelism\n'), 'resource selection');
    const configure = onlyStep(steps, step => step.startsWith(`      - name: ${configureName}\n`), 'configure step');
    assert.ok(resources.index < configure.index);
    assert.match(resources.step, /^        run: node \.github\/scripts\/build-parallelism\.mjs$/m);
    assert.doesNotMatch(resources.step, /^        (?:continue-on-error|if):/m);
    if (name === 'applications') {
      const [args] = applicationCommands('configure', { VCPKG_ROOT: '/vcpkg', ARK_CMAKE_JOB_POOLS: 'ark_compile=4;ark_link=1' });
      for (const option of ['-DCMAKE_JOB_POOLS=ark_compile=4;ark_link=1',
        '-DCMAKE_JOB_POOL_COMPILE=ark_compile', '-DCMAKE_JOB_POOL_LINK=ark_link']) assert.ok(args.includes(option));
    } else {
      assert.ok(configure.step.includes('"-DCMAKE_JOB_POOLS=$env:ARK_CMAKE_JOB_POOLS"'));
      assert.match(configure.step, /-DCMAKE_JOB_POOL_COMPILE=ark_compile/);
      assert.match(configure.step, /-DCMAKE_JOB_POOL_LINK=ark_link/);
      assert.ok(sourceBuild(steps, name, buildDirectory).step.includes('--parallel ${{ steps.build-parallelism.outputs.build_jobs }}'));
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
      if (name === 'applications') {
        const [args] = applicationCommands('configure', { VCPKG_ROOT: '/vcpkg', ARK_COMPILER_LAUNCHER: '/tool cache/sccache' });
        assert.ok(args.includes(`-DCMAKE_${language}_COMPILER_LAUNCHER=/tool cache/sccache`));
      } else {
        assert.ok(configure.step.includes(`"-DCMAKE_${language}_COMPILER_LAUNCHER=$env:ARK_COMPILER_LAUNCHER"`));
      }
    }
    assert.match(save.step, /steps\.compiler-start\.outputs\.save_enabled == 'true'/);
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
      { saveEnabled: 'false' },
      { saveEnabled: '' },
      ...['failure', 'cancelled', 'skipped', ''].map(outcome => ({ outcome })),
      { cacheHit: 'true' },
      { previous: ['success', 'failure'], cacheHit: 'true' },
    ]) {
      assert.equal(compilerSaveRuns(step, scenario), false, JSON.stringify(scenario));
    }
    assert.equal(compilerSaveRuns(step, { cacheHit: '' }), true, 'a successful restore miss may omit cache-hit');
  });

  test(`${name} restricts compiler writes to authorized PR snapshots or trusted main`, () => {
    const { step } = onlyStep(jobSteps(name), step => step.includes('name: Save compiler cache\n'), 'compiler save');
    for (const eventName of ['push', 'workflow_dispatch']) {
      assert.equal(compilerSaveRuns(step, { eventName, ref: 'refs/heads/main' }), true);
      for (const ref of ['refs/heads/feature', 'refs/tags/main', 'refs/pull/1/merge', '']) {
        assert.equal(compilerSaveRuns(step, { eventName, ref }), false, `${eventName}: ${ref}`);
      }
      assert.equal(compilerSaveRuns(step, { eventName, ref: 'refs/heads/main', saveEnabled: 'false' }), false);
    }
    for (const eventName of ['merge_group', 'pull_request_target', 'schedule', 'workflow_run', '']) {
      assert.equal(compilerSaveRuns(step, { eventName, ref: 'refs/heads/main' }), false, eventName);
    }
    assert.equal(compilerSaveRuns(step, { saveEnabled: 'false' }), false, 'PR writes also require helper authorization');
  });
}

test('scope plans reach the application runner while queue and manual runs retain ALL and both full checks', () => {
  assert.match(workflow, /^      app_plan_json: \$\{\{ steps\.scope\.outputs\.app_plan_json \}\}$/m);
  const applicationJob = workflow.split(/(?=^  [a-z][a-z0-9_-]*:$)/m).find(block => block.startsWith('  applications:\n'));
  for (const [variable, output] of [['ARK_APPLICATION_PLAN', 'app_plan_json'],
    ['ARK_APPLICATION_VALIDATION_STAGE', 'validation_stage'], ['ARK_APPLICATION_FILES_KNOWN', 'files_known']]) {
    assert.ok(applicationJob.includes(`${variable}: \${{ needs.scope.outputs.${output} }}`));
  }
  assert.ok(applicationJob.includes('ARK_APPLICATION_SCOPE_RESULT: ${{ needs.scope.result }}'));
  for (const eventName of ['pull_request', 'merge_group', 'workflow_dispatch']) {
    const scope = selectScope({ eventName, files: ['tests/app/shutdown_deadlines.cmake'], site: { present: true, available: true } });
    const env = { GITHUB_EVENT_NAME: eventName, ARK_APPLICATION_PLAN: scope.app_plan_json, ARK_APPLICATION_SCOPE_RESULT: 'success',
      ARK_APPLICATION_VALIDATION_STAGE: scope.validation_stage, ARK_APPLICATION_FILES_KNOWN: String(scope.files_known), ARK_BUILD_JOBS: '4' };
    if (eventName === 'pull_request') {
      assert.deepEqual(applicationCommands('build', env), []);
      assert.deepEqual(applicationCommands('check', env), [
        ['--build', 'ci-build', '--target', 'check-app-shutdown-deadlines', '--parallel', '4'],
      ]);
    } else {
      assert.deepEqual(applicationCommands('build', env), [['--build', 'ci-build', '--parallel', '4']]);
      assert.deepEqual(applicationCommands('check', env), [
        ['--build', 'ci-build', '--target', 'check-core', '--parallel', '4'],
        ['--build', 'ci-build', '--target', 'check-entity-benchmark-smoke', '--parallel', '4'],
      ]);
    }
  }
});

test('optional application caching preserves direct compilers, Windows Debug settings and fatal native results', () => {
  for (const runner of ['Linux', 'macOS', 'Windows']) {
    const env = { GITHUB_EVENT_NAME: 'merge_group', RUNNER_OS: runner, VCPKG_ROOT: '/vcpkg', ARK_COMPILER_LAUNCHER: '' };
    const [args] = applicationCommands('configure', env);
    assert.ok(args.includes('-DCMAKE_BUILD_TYPE=Debug'));
    for (const language of ['C', 'CXX']) assert.ok(args.includes(`-DCMAKE_${language}_COMPILER_LAUNCHER=`));
    assert.equal(args.includes('-DVCPKG_APPLOCAL_DEPS=OFF'), runner === 'Windows');
    assert.ok(!args.some(arg => /CMAKE_(?:C|CXX)_FLAGS|\/(?:Zi|Z7)\b/.test(arg)));
    for (const action of ['configure', 'build', 'check']) {
      const calls = [];
      const status = runApplications(action, env, (command, commandArgs, options) => {
        calls.push({ command, commandArgs, options });
        return { status: 37 };
      });
      assert.equal(status, 37, `${runner} ${action}`);
      assert.equal(calls.length, 1, 'stop immediately after a failed native command');
      assert.equal(calls[0].command, 'cmake');
      assert.equal(calls[0].options.shell, false);
    }
  }
});

test('compiler seeding accepts only main pushes or manual main runs with read-only repository access', () => {
  const triggers = compilerSeed.match(/^on:\n([\s\S]*?)(?=^permissions:)/m)?.[1];
  assert.ok(triggers);
  assert.deepEqual([...triggers.matchAll(/^  (\w+):/gm)].map(match => match[1]), ['push', 'workflow_dispatch']);
  assert.match(triggers, /^  push:\n    branches: \[main\]\n    paths:\n/m);
  assert.doesNotMatch(triggers, /schedule:|pull_request|merge_group|workflow_run|repository_dispatch/);
  assert.match(compilerSeed, /^permissions:\n  contents: read\n/m);
  assert.equal([...compilerSeed.matchAll(/^ *permissions:/gm)].length, 1);
  assert.match(compilerSeed, /persist-credentials: false/);
  assert.doesNotMatch(compilerSeed, /secrets\.|secrets:|pull_request_target|upload-artifact|gh api|curl /);
  assert.match(compilerSeed, /COMPILER_CACHE_WRITE: 'true'/);
  assert.match(compilerSeed, /COMPILER_CACHE_PROFILE: applications/);
  assert.deepEqual([...compilerSeed.matchAll(/^          - os: (.+)$/gm)].map(match => match[1]), ['ubuntu-latest', 'macos-latest']);
  const guard = compilerSeed.match(/^    if: \$\{\{ (.+) \}\}$/m)?.[1];
  assert.ok(guard);
  for (const ref of ['refs/heads/main', 'refs/heads/feature', 'refs/pull/1/merge', 'refs/tags/main', '']) {
    assert.equal(runInNewContext(guard, { github: { ref } }), ref === 'refs/heads/main', ref);
  }
});

test('compiler seeding requires trusted cache inputs, successful native compilation and exact saved availability', () => {
  const steps = jobSteps('applications', compilerSeed);
  const current = onlyStep(steps, step => step.includes('id: current\n'), 'current main selection');
  assert.match(current.step, /run: node \.github\/scripts\/compiler-seed-plan\.mjs/);
  const trusted = onlyStep(steps, step => step.includes('name: Require trusted compiler cache inputs\n'), 'trusted inputs');
  assert.match(trusted.step, /CACHE_ENABLED: \$\{\{ steps\.compiler-start\.outputs\.enabled \}\}/);
  assert.match(trusted.step, /CACHE_WRITE_ENABLED: \$\{\{ steps\.compiler-start\.outputs\.save_enabled \}\}/);
  assert.match(trusted.step, /test "\$CACHE_ENABLED" = true\n          test "\$CACHE_WRITE_ENABLED" = true/);
  const configure = onlyStep(steps, step => step.includes('run: node .github/scripts/ci-applications.mjs configure\n'), 'seed configure');
  const build = sourceBuild(steps, 'applications', 'ci-build');
  assert.ok(current.index < trusted.index && trusted.index < configure.index && configure.index < build.index);
  for (const { step } of [trusted, configure, build]) assert.doesNotMatch(step, /continue-on-error:/);
  assert.match(build.step, /steps\.current\.outputs\.build == 'true'/);
  assert.deepEqual(applicationCommands('build', { GITHUB_EVENT_NAME: 'push', ARK_BUILD_JOBS: '4' }),
    [['--build', 'ci-build', '--parallel', '4']]);
  const save = onlyStep(steps, step => step.includes('name: Save compiler cache\n'), 'seed save');
  const verify = onlyStep(steps, step => step.includes('name: Verify trusted main cache availability\n'), 'exact verification');
  assert.ok(build.index < save.index && save.index < verify.index);
  for (const { step } of [save, verify]) {
    assert.doesNotMatch(step, /continue-on-error:/);
    assert.match(step, /path: \$\{\{ steps\.compiler-cache\.outputs\.directory \}\}/);
    assert.match(step, /key: \$\{\{ steps\.compiler-cache\.outputs\.archive_key \}\}/);
    assert.doesNotMatch(step, /restore-keys:/);
    assert.match(step, /steps\.compiler-start\.outputs\.enabled == 'true'/);
    assert.match(step, /steps\.compiler-start\.outputs\.save_enabled == 'true'/);
  }
  assert.match(verify.step, /lookup-only: true\n          fail-on-cache-miss: true/);
  assert.match(verify.step, /steps\.current\.outputs\.build == 'true'/);
  for (const eventName of ['push', 'workflow_dispatch', 'pull_request', 'merge_group', 'schedule']) {
    for (const ref of ['refs/heads/main', 'refs/heads/feature']) {
      for (const saveEnabled of ['true', 'false', '']) {
        for (const enabled of ['true', 'false', '']) {
          for (const success of [true, false]) {
            const saved = runInNewContext(conditionExpression(save.step), {
              success: () => success, github: { event_name: eventName, ref },
              steps: { 'compiler-start': { outputs: { enabled, save_enabled: saveEnabled } },
                'compiler-restore': { outputs: { 'cache-hit': 'false' } } },
            });
            assert.equal(saved, success && ref === 'refs/heads/main' && ['push', 'workflow_dispatch'].includes(eventName) &&
              enabled === 'true' && saveEnabled === 'true');
          }
        }
      }
    }
  }
  assert.equal(runInNewContext(conditionExpression(save.step), {
    success: () => true, github: { event_name: 'push', ref: 'refs/heads/main' },
    steps: { 'compiler-start': { outputs: { enabled: 'true', save_enabled: 'true' } },
      'compiler-restore': { outputs: { 'cache-hit': 'true' } } },
  }), false, 'an immutable exact hit cannot be replaced');
  for (const build of ['true', 'false']) {
    for (const enabled of ['true', 'false', '']) {
      for (const saveEnabled of ['true', 'false', '']) {
        assert.equal(runInNewContext(conditionExpression(verify.step), {
          steps: { current: { outputs: { build } },
            'compiler-start': { outputs: { enabled, save_enabled: saveEnabled } } },
        }), build === 'true' && enabled === 'true' && saveEnabled === 'true');
      }
    }
  }
});

test('optional compiler reuse measurements preserve clean, cache-reset and rebuild failures', () => {
  const steps = jobSteps('applications', compilerSeed);
  const configure = onlyStep(steps, step => step.includes('run: node .github/scripts/ci-applications.mjs configure\n'), 'seed configure');
  const trusted = onlyStep(steps, step => step.includes('name: Require trusted compiler cache inputs\n'), 'trusted inputs');
  const cold = onlyStep(steps, step => step.includes('name: Prepare controlled cold-cache measurement\n'), 'controlled cold measurement');
  const build = sourceBuild(steps, 'applications', 'ci-build');
  const reuse = onlyStep(steps,
    step => step.includes('name: Measure reuse after cleaning project outputs\n'), 'reuse measurement');
  const { step } = reuse;
  assert.ok(trusted.index < configure.index && configure.index < cold.index && cold.index < build.index && build.index < reuse.index);
  assert.match(cold.step, /& \$env:SCCACHE_PATH --stop-server\n          if \(\$LASTEXITCODE -ne 0\) \{ exit \$LASTEXITCODE \}/);
  assert.match(cold.step, /^          Remove-Item -LiteralPath \$env:SCCACHE_DIR -Recurse -Force$/m);
  assert.equal((cold.step.match(/Remove-Item/g) ?? []).length, 1, 'clear only the validated local cache directory');
  assert.match(cold.step, /& \$env:SCCACHE_PATH --start-server\n          if \(\$LASTEXITCODE -ne 0\) \{ exit \$LASTEXITCODE \}/);
  assert.match(cold.step, /& \$env:SCCACHE_PATH --zero-stats\n          if \(\$LASTEXITCODE -ne 0\) \{ exit \$LASTEXITCODE \}/);
  for (const [eventName, verifyReuse] of [['push', undefined], ['workflow_dispatch', false], ['workflow_dispatch', true]]) {
    for (const buildCurrent of ['true', 'false']) {
      const context = { github: { event_name: eventName }, inputs: { verify_reuse: verifyReuse },
        steps: { current: { outputs: { build: buildCurrent } } } };
      const expected = eventName === 'workflow_dispatch' && verifyReuse === true && buildCurrent === 'true';
      assert.equal(runInNewContext(conditionExpression(cold.step), context), expected);
      assert.equal(runInNewContext(conditionExpression(step), context), expected);
    }
  }
  assert.match(step, /inputs\.verify_reuse == true/);
  assert.match(step, /cmake --build ci-build --target clean\n          if \(\$LASTEXITCODE -ne 0\) \{ exit \$LASTEXITCODE \}/);
  assert.match(step, /& \$env:SCCACHE_PATH --zero-stats\n          if \(\$LASTEXITCODE -ne 0\) \{ exit \$LASTEXITCODE \}/);
  assert.match(step, /node \.github\/scripts\/ci-applications\.mjs build\n          if \(\$LASTEXITCODE -ne 0\) \{ exit \$LASTEXITCODE \}/);
  for (const value of [cold.step, step]) {
    assert.doesNotMatch(value, /continue-on-error:|ci-applications\.mjs configure|cmake -S|gh |curl |cache.*delete/);
  }
  assert.doesNotMatch(step, /Remove-Item|--stop-server|--start-server/);
  assert.equal((compilerSeed.match(/run: node \.github\/scripts\/ci-applications\.mjs configure/g) ?? []).length, 1);
  assert.equal((compilerSeed.match(/node \.github\/scripts\/ci-applications\.mjs build/g) ?? []).length, 2,
    'cold and warm builds share one configure and one runner implementation');
});
