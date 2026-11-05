import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import test from 'node:test';
import { reportCacheStats } from '../../.github/scripts/cache-stats.mjs';

test('seed diagnostics distinguish a restored snapshot from whether this revision was built', () => {
  const env = { COMPILER_ARCHIVE_HIT: 'true', ARK_CACHE_SAMPLE: 'seed-or-restored-snapshot',
    ARK_CACHE_BUILD_EXECUTED: 'false', GITHUB_SHA: 'a'.repeat(40),
    ARK_CACHE_ARCHIVE_KEY: 'ark-sccache-0.16.0-v2-Linux-trusted-2026-W45' };
  const report = reportCacheStats({ env, log: () => {} });
  assert.match(report, /Compiler archive cache exact hit: true/);
  assert.match(report, /Cache measurement sample: seed-or-restored-snapshot/);
  assert.match(report, /Project build executed: false/);
  assert.ok(report.includes(`Source revision: ${env.GITHUB_SHA}`));
  assert.ok(report.includes(`Requested archive key: ${env.ARK_CACHE_ARCHIVE_KEY}`));
});

test('cache diagnostics distinguish archive restoration, configured budget, policy and actual object statistics', t => {
  const root = mkdtempSync(join(tmpdir(), 'ark-cache-stats-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  mkdirSync(join(root, 'nested'));
  writeFileSync(join(root, 'one.zip'), 'fixture');
  writeFileSync(join(root, 'nested', 'two.zip'), 'fixture');
  const summary = join(root, 'summary');
  const env = { VCPKG_ARCHIVE_DIRECTORY: root, VCPKG_ARCHIVE_HIT: 'true', COMPILER_ARCHIVE_HIT: 'false',
    ARK_COMPILER_LAUNCHER: '/fixture/sccache', SCCACHE_CACHE_SIZE: '768M',
    ARK_COMPILER_CACHE_SAVE_ENABLED: 'false', ARK_COMPILER_CACHE_ARCHIVE_SCOPE: 'trusted',
    ARK_COMPILER_CACHE_EPOCH: '2026-W45', GITHUB_STEP_SUMMARY: summary };
  const messages = [];
  const report = reportCacheStats({ env, log: text => messages.push(text), run: (command, args, options) => {
    assert.equal(command, env.ARK_COMPILER_LAUNCHER);
    assert.deepEqual(args, ['--show-stats']);
    assert.equal(options.env, env);
    assert.equal(options.timeout, 30_000);
    return { status: 0, stdout: 'Compile requests 409\nCache hits 29\n' };
  } });
  assert.match(report, /Dependency archives after configure\/build: 2/);
  assert.match(report, /Compiler archive cache exact hit: false/);
  assert.match(report, /Compiler local cache budget: 768M/);
  assert.match(report, /publication allowed: false \(scope=trusted, epoch=2026-W45\)/);
  assert.match(report, /Compile requests 409\nCache hits 29/);
  assert.deepEqual(messages, [report]);
  assert.ok(readFileSync(summary, 'utf8').includes(report));
});

test('unavailable statistics, missing storage and summary errors never change the native result', () => {
  for (const run of [
    () => { throw new Error('tool unavailable'); },
    () => ({ status: 1, stdout: '' }),
    () => ({ status: null, error: new Error('timeout') }),
    () => ({ status: 0, stdout: null }),
    () => null,
  ]) {
    const report = reportCacheStats({ env: { ARK_COMPILER_LAUNCHER: '/missing/sccache',
      VCPKG_ARCHIVE_DIRECTORY: '/missing/archives', GITHUB_STEP_SUMMARY: '/missing/summary' },
      run, append: () => { throw new Error('summary unavailable'); }, log: () => {} });
    assert.match(report, /sccache statistics unavailable/);
    assert.match(report, /Dependency archives after configure\/build: unavailable/);
  }
});

test('disabled caching reports direct compilation without launching tools', () => {
  const report = reportCacheStats({ env: {}, log: () => {}, run: () => assert.fail('disabled cache must not invoke a tool') });
  assert.match(report, /compilation uses the compiler directly/);
  assert.match(report, /Compiler local cache budget: unavailable/);
});

test('optional compiler diagnostics remain bounded', () => {
  const report = reportCacheStats({ env: { ARK_COMPILER_LAUNCHER: '/fixture/sccache' }, log: () => {},
    run: () => ({ status: 0, stdout: 'x'.repeat(32 * 1024) }) });
  assert.ok(report.length < 17 * 1024);
  assert.match(report, /\[sccache output truncated\]/);
});
