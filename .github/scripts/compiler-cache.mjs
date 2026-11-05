import { spawnSync } from 'node:child_process';
import { appendFileSync, lstatSync, mkdirSync } from 'node:fs';
import { posix, resolve, win32 } from 'node:path';
import { pathToFileURL } from 'node:url';

export const sccacheVersion = '0.16.0';
const singleLine = value => typeof value === 'string' && !/[\x00-\x1f\x7f]/.test(value);
const cold = reason => ({ compatible: false, reason, directory: '', key: '', archiveKey: '', restoreKeys: '',
  cacheSize: '512M', epoch: '', scope: '', saveEnabled: false });

function weeklyEpoch(now) {
  const date = new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), now.getUTCDate()));
  date.setUTCDate(date.getUTCDate() + 4 - (date.getUTCDay() || 7));
  const year = date.getUTCFullYear();
  const week = Math.ceil(((date - Date.UTC(year, 0, 1)) / 86400000 + 1) / 7);
  return `${year}-W${String(week).padStart(2, '0')}`;
}

export function compilerCachePlan({ env, now = new Date() }) {
  if (!singleLine(env.COMPILER_CACHE_IDENTITY) || !singleLine(env.GITHUB_SHA)) {
    return cold('missing or invalid compatibility identity');
  }
  const identity = env.COMPILER_CACHE_IDENTITY.match(
    /^ark-vcpkg-files-v2-(Linux|macOS|Windows)-(X64|ARM64|ARM)-([a-f0-9]{64})-([a-f0-9]{64})$/);
  if (!identity || identity[1] !== env.RUNNER_OS || identity[2] !== env.RUNNER_ARCH ||
      !/^[a-f0-9]{40}$/.test(env.GITHUB_SHA ?? '')) return cold('missing or invalid compatibility identity');
  // Keep /Zi and the deliberately uncached Windows Debug configuration unchanged.
  if (env.RUNNER_OS === 'Windows') return cold('Windows compilation uses the compiler directly');
  const paths = env.RUNNER_OS === 'Windows' ? win32 : posix;
  if (!paths.isAbsolute(env.RUNNER_TEMP ?? '') || /[\x00-\x1f\x7f;,`*?\[\]{}!]/.test(env.RUNNER_TEMP ?? '')) {
    return cold('unsupported compiler cache directory');
  }
  const profile = env.COMPILER_CACHE_PROFILE ?? '';
  if (!singleLine(profile) || !/^(applications|network-(none|address|thread)|database-(Boost|Standalone))$/.test(profile)) {
    return cold('missing or invalid compiler cache profile');
  }
  const capacity = env.COMPILER_CACHE_MAX_MIB ?? '512';
  if (!singleLine(capacity) || !/^[1-9][0-9]*$/.test(capacity) || Number(capacity) < 64 || Number(capacity) > 1024) {
    return cold('compiler cache capacity must be an integer from 64 through 1024 MiB');
  }
  const epoch = env.COMPILER_CACHE_EPOCH ?? weeklyEpoch(now);
  if (!singleLine(epoch) || !/^[0-9]{4}-W(0[1-9]|[1-4][0-9]|5[0-3])$/.test(epoch)) return cold('invalid compiler cache epoch');
  if (env.COMPILER_CACHE_WRITE !== undefined && !['true', 'false'].includes(env.COMPILER_CACHE_WRITE)) {
    return cold('invalid compiler cache write policy');
  }
  const trustedMain = env.GITHUB_REF === 'refs/heads/main' && ['push', 'workflow_dispatch'].includes(env.GITHUB_EVENT_NAME);
  const sourcePullRequest = env.GITHUB_EVENT_NAME === 'pull_request' && singleLine(env.GITHUB_REF) &&
    /^refs\/pull\/[1-9][0-9]*\/merge$/.test(env.GITHUB_REF);
  const scope = sourcePullRequest ? 'untrusted' : 'trusted';
  // The dependency family retains OS/imageOS, architecture, compiler environment,
  // manifest/overlay, ABI triplets and module/sanitizer profile. Only ImageVersion
  // is omitted here; sccache still hashes the actual compiler, flags and inputs.
  const key = `ark-sccache-${sccacheVersion}-v2-${identity[1]}-${identity[2]}-${identity[3]}`;
  const prefix = `${key}-${scope}-`;
  const restoreKeys = scope === 'untrusted' ? `${prefix}\n${key}-trusted-` : prefix;
  // Applications are seeded only by trusted main. Other source-PR profiles retain
  // one immutable snapshot per ref/family/week; queue jobs never multiply saves.
  // The workflow must independently enforce its trusted checkout/event boundary.
  const saveEnabled = env.COMPILER_CACHE_WRITE !== 'false' &&
    ((trustedMain && env.COMPILER_CACHE_WRITE === 'true') || (sourcePullRequest && profile !== 'applications'));
  return { compatible: true, reason: '', directory: paths.join(env.RUNNER_TEMP, 'ark-sccache'), key,
    archiveKey: `${prefix}${epoch}`, restoreKeys, cacheSize: `${capacity}M`, epoch, scope, saveEnabled };
}

export function prepareCompilerCache({ env, run = spawnSync, start = true, now }) {
  if (!env.GITHUB_ENV || !env.GITHUB_OUTPUT) throw new Error('GitHub file-command channels are required');
  const selection = compilerCachePlan({ env, now });
  const paths = env.RUNNER_OS === 'Windows' ? win32 : posix;
  let enabled = false;
  if (selection.compatible && (!start || env.SCCACHE_RESTORE_OUTCOME === 'success') &&
      env.SCCACHE_SETUP_OUTCOME === 'success' && env.SCCACHE_PATH &&
      paths.isAbsolute(env.SCCACHE_PATH) && !/[\x00-\x1f\x7f;]/.test(env.SCCACHE_PATH)) {
    try {
      mkdirSync(selection.directory, { recursive: true });
      if (!lstatSync(selection.directory).isDirectory()) throw new Error('cache path is not a directory');
      const cacheEnv = { ...env, SCCACHE_DIR: selection.directory, SCCACHE_GHA_ENABLED: 'false',
        SCCACHE_IGNORE_SERVER_IO_ERROR: '1', SCCACHE_CACHE_SIZE: selection.cacheSize,
        SCCACHE_C_CUSTOM_CACHE_BUSTER: selection.key };
      const options = { env: cacheEnv, encoding: 'utf8', timeout: 30_000 };
      const version = run(env.SCCACHE_PATH, ['--version'], options);
      if (version.status === 0 && version.stdout?.trim() === `sccache ${sccacheVersion}`) {
        enabled = !start || run(env.SCCACHE_PATH, ['--start-server'], options).status === 0;
      }
    } catch {
      // Optional cache setup must not prevent a normal compiler invocation.
    }
  }
  const launcher = enabled ? env.SCCACHE_PATH.replaceAll('\\', '/') : '';
  const saveEnabled = enabled && selection.saveEnabled;
  appendFileSync(env.GITHUB_ENV, `ARK_COMPILER_LAUNCHER=${launcher}\nSCCACHE_DIR=${enabled ? selection.directory : ''}\nSCCACHE_GHA_ENABLED=false\nSCCACHE_IGNORE_SERVER_IO_ERROR=1\nSCCACHE_CACHE_SIZE=${selection.cacheSize}\nSCCACHE_C_CUSTOM_CACHE_BUSTER=${enabled ? selection.key : ''}\nARK_COMPILER_CACHE_SAVE_ENABLED=${saveEnabled}\nARK_COMPILER_CACHE_EPOCH=${enabled ? selection.epoch : ''}\nARK_COMPILER_CACHE_ARCHIVE_SCOPE=${enabled ? selection.scope : ''}\n`);
  const output = `enabled=${enabled}\ndirectory=${enabled ? selection.directory : ''}\nkey=${enabled ? selection.key : ''}\narchive_key=${enabled ? selection.archiveKey : ''}\nsave_enabled=${saveEnabled}\ncache_size=${selection.cacheSize}\nepoch=${enabled ? selection.epoch : ''}\narchive_scope=${enabled ? selection.scope : ''}\n`;
  // The keys contain only validated fixed-format characters, so this delimiter
  // cannot be injected through a path, profile, source revision or epoch.
  appendFileSync(env.GITHUB_OUTPUT, output + (enabled ?
    `restore_keys<<ARK_SCCACHE_RESTORE_KEYS\n${selection.restoreKeys}\nARK_SCCACHE_RESTORE_KEYS\n` : 'restore_keys=\n'));
  return { ...selection, enabled, launcher, saveEnabled };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    const plan = prepareCompilerCache({ env: process.env, start: !process.argv.includes('--prepare') });
    console.log(plan.enabled ? `sccache ${sccacheVersion} prepared (${plan.cacheSize}, ${plan.scope}, save=${plan.saveEnabled}, ${plan.epoch})` :
      `Compiler cache unavailable${plan.reason ? `: ${plan.reason}` : ''}; using the compiler directly`);
  } catch {
    console.error('Unable to export compiler cache configuration to GitHub file-command channels');
    process.exitCode = 1;
  }
}
