import { spawnSync } from 'node:child_process';
import { appendFileSync, readdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

export function reportCacheStats({ env = process.env, run = spawnSync, append = appendFileSync, log = console.log } = {}) {
  let archives = 'unavailable';
  if (env.VCPKG_ARCHIVE_DIRECTORY) {
    try {
      archives = readdirSync(env.VCPKG_ARCHIVE_DIRECTORY, { recursive: true })
        .filter(file => file.endsWith('.zip')).length;
    } catch {
      // Cache diagnostics do not change the build or test result.
    }
  }
  const cacheSize = /^(?:[1-9][0-9]*)M$/.test(env.SCCACHE_CACHE_SIZE ?? '') ? env.SCCACHE_CACHE_SIZE : 'unavailable';
  const scope = ['trusted', 'untrusted'].includes(env.ARK_COMPILER_CACHE_ARCHIVE_SCOPE) ? env.ARK_COMPILER_CACHE_ARCHIVE_SCOPE : 'unavailable';
  const epoch = /^[0-9]{4}-W[0-9]{2}$/.test(env.ARK_COMPILER_CACHE_EPOCH ?? '') ? env.ARK_COMPILER_CACHE_EPOCH : 'unavailable';
  const lines = [
    `Dependency archive cache exact hit: ${env.VCPKG_ARCHIVE_HIT || 'false/unavailable'}`,
    `Dependency archives after configure/build: ${archives}`,
    'vcpkg package restore/build counts are in the Configure step output.',
    `Compiler archive cache exact hit: ${env.COMPILER_ARCHIVE_HIT || 'false/unavailable'} (a prefix restore can still provide compiler hits)`,
    `Compiler local cache budget: ${cacheSize}`,
    `Compiler archive publication allowed: ${env.ARK_COMPILER_CACHE_SAVE_ENABLED === 'true'} (scope=${scope}, epoch=${epoch})`,
  ];
  if (['controlled-cold', 'controlled-warm', 'seed-or-restored-snapshot'].includes(env.ARK_CACHE_SAMPLE)) {
    const source = /^[a-f0-9]{40}$/.test(env.GITHUB_SHA ?? '') ? env.GITHUB_SHA : 'unavailable';
    const key = /^ark-sccache-[a-zA-Z0-9.-]{1,240}$/.test(env.ARK_CACHE_ARCHIVE_KEY ?? '')
      ? env.ARK_CACHE_ARCHIVE_KEY : 'unavailable';
    lines.unshift(`Cache measurement sample: ${env.ARK_CACHE_SAMPLE}`, `Source revision: ${source}`,
      `Requested archive key: ${key}`,
      `Project build executed: ${['true', 'false'].includes(env.ARK_CACHE_BUILD_EXECUTED)
        ? env.ARK_CACHE_BUILD_EXECUTED : 'unavailable'}`);
  }
  if (env.ARK_COMPILER_LAUNCHER) {
    let statistics = 'sccache statistics unavailable';
    try {
      const result = run(env.ARK_COMPILER_LAUNCHER, ['--show-stats'], {
        env, encoding: 'utf8', timeout: 30_000,
      });
      if (result?.status === 0 && typeof result.stdout === 'string' && result.stdout.trim()) {
        const output = result.stdout.trim();
        statistics = output.slice(0, 16 * 1024) + (output.length > 16 * 1024 ? '\n[sccache output truncated]' : '');
      }
    } catch {
      // An unavailable optional tool cannot turn successful native checks red.
    }
    lines.push(statistics);
  } else {
    lines.push('Compiler cache disabled; compilation uses the compiler directly.');
  }
  const report = lines.join('\n');
  log(report);
  if (env.GITHUB_STEP_SUMMARY) {
    try {
      append(env.GITHUB_STEP_SUMMARY, `\n### Native build cache\n\n\`\`\`text\n${report}\n\`\`\`\n`);
    } catch {
      // The console still contains the report when a summary is unavailable.
    }
  }
  return report;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) reportCacheStats();
