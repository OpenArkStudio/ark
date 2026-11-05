import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import {
  applicationCheckTargets, applicationConfigureFlags,
  fullApplicationBuildPlan,
} from './application-build-plan.mjs';

export function applicationPlan(env) {
  const fallback = () => fullApplicationBuildPlan('runner-full-fallback');
  if (env.GITHUB_EVENT_NAME !== 'pull_request' || env.ARK_APPLICATION_SCOPE_RESULT !== 'success' ||
      env.ARK_APPLICATION_VALIDATION_STAGE !== 'modules' ||
      env.ARK_APPLICATION_FILES_KNOWN !== 'true') return fallback();
  try {
    const plan = JSON.parse(env.ARK_APPLICATION_PLAN ?? '');
    const knownTargets = new Set(applicationCheckTargets);
    if (plan.version !== 1 || !['full', 'targets'].includes(plan.mode) ||
        typeof plan.reason !== 'string' || plan.buildAll !== (plan.mode === 'full') ||
        !Array.isArray(plan.configureFlags) ||
        JSON.stringify(plan.configureFlags) !== JSON.stringify(applicationConfigureFlags) ||
        !Array.isArray(plan.checkTargets) || plan.checkTargets.length === 0 ||
        new Set(plan.checkTargets).size !== plan.checkTargets.length ||
        plan.checkTargets.some(target => !knownTargets.has(target)) ||
        (plan.buildAll && JSON.stringify(plan.checkTargets) !==
          JSON.stringify(['check-core', 'check-entity-benchmark-smoke']))) return fallback();
    return plan;
  } catch {
    return fallback();
  }
}

export function applicationCommands(action, env) {
  const plan = applicationPlan(env);
  const jobs = /^[1-4]$/.test(env.ARK_BUILD_JOBS ?? '') ? env.ARK_BUILD_JOBS : '2';
  if (action === 'configure') {
    if (!env.VCPKG_ROOT) throw new Error('VCPKG_ROOT is required');
    const args = ['-S', '.', '-B', 'ci-build', '-G', 'Ninja',
      `-DCMAKE_TOOLCHAIN_FILE=${resolve(env.VCPKG_ROOT, 'scripts/buildsystems/vcpkg.cmake')}`,
      '-DVCPKG_MANIFEST_FEATURES=application;boost;compression-tests;etcd-client;guest-room;config-tool',
      '-DCMAKE_BUILD_TYPE=Debug',
      `-DCMAKE_C_COMPILER_LAUNCHER=${env.ARK_COMPILER_LAUNCHER ?? ''}`,
      `-DCMAKE_CXX_COMPILER_LAUNCHER=${env.ARK_COMPILER_LAUNCHER ?? ''}`,
      `-DCMAKE_JOB_POOLS=${env.ARK_CMAKE_JOB_POOLS ?? 'ark_compile=2;ark_link=1'}`,
      '-DCMAKE_JOB_POOL_COMPILE=ark_compile', '-DCMAKE_JOB_POOL_LINK=ark_link'];
    if (env.RUNNER_OS === 'Windows') args.push('-DVCPKG_APPLOCAL_DEPS=OFF');
    return [[...args, ...plan.configureFlags]];
  }
  if (action === 'build') {
    return plan.buildAll ? [['--build', 'ci-build', '--parallel', jobs]] : [];
  }
  if (action === 'check') {
    return plan.checkTargets.map(target => ['--build', 'ci-build', '--target', target, '--parallel', jobs]);
  }
  throw new Error(`Unknown applications phase: ${action}`);
}

export function runApplications(action, env, run = spawnSync) {
  const plan = applicationPlan(env);
  console.log(`Applications plan: ${plan.mode}; ${plan.reason}; ${plan.checkTargets.join(', ')}`);
  const started = performance.now();
  for (const args of applicationCommands(action, env)) {
    const result = run('cmake', args, { env, stdio: 'inherit', shell: false });
    if (result.error) throw result.error;
    if (result.status !== 0) return Number.isInteger(result.status) ? result.status : 1;
  }
  console.log(`Applications ${action} completed in ${((performance.now() - started) / 1000).toFixed(2)} seconds`);
  return 0;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    process.exitCode = runApplications(process.argv[2], process.env);
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
