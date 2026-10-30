import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync, realpathSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import { test } from 'node:test';

const root = resolve(import.meta.dirname, '../..');
const run = join(root, 'deployment/scripts/run.sh');
const stop = join(root, 'deployment/scripts/kill.sh');

function fixture(t) {
  const dir = mkdtempSync(join(tmpdir(), 'ark runtime '));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  mkdirSync(join(dir, 'bin'));
  mkdirSync(join(dir, 'conf'));
  writeFileSync(join(dir, 'conf/timers.app.toml'), '[application]\n');
  const binary = '#!/bin/sh\nprintf "%s\\n" "$PWD" "$LC_ALL" "$0" "$@"\nexit 17\n';
  for (const name of ['app', 'app_d']) {
    writeFileSync(join(dir, 'bin', name), binary, { mode: 0o755 });
  }
  return dir;
}

for (const [configuration, binary] of [['Release', 'app'], ['Debug', 'app_d']]) {
  test(`${configuration} selects one application in the explicit runtime directory`, (t) => {
    const dir = fixture(t);
    const result = spawnSync('sh', [run, configuration, 'timers', '1.0.1.1', dir], {
      cwd: tmpdir(), encoding: 'utf8',
    });
    assert.equal(result.status, 17, result.stderr);
    const [cwd, locale, executable, ...args] = result.stdout.trim().split('\n');
    assert.equal(realpathSync(cwd), realpathSync(dir));
    assert.equal(locale, 'C');
    assert.equal(realpathSync(resolve(cwd, executable)), realpathSync(join(dir, 'bin', binary)));
    assert.deepEqual(args, [
      '--busid=1.0.1.1', '--name=timers', '--app_conf=conf/timers.app.toml',
    ]);
  });
}

test('default runtime is the script directory and ignores the caller working directory', (t) => {
  const dir = fixture(t);
  writeFileSync(join(dir, 'run.sh'), readFileSync(run));
  const result = spawnSync('sh', [join(dir, 'run.sh'), 'Release', 'timers', '1.0.1.1'], {
    cwd: tmpdir(), encoding: 'utf8',
  });
  assert.equal(result.status, 17, result.stderr);
  assert.match(result.stdout, /--app_conf=conf\/timers\.app\.toml/);
});

test('missing configuration or executable fails before launching', (t) => {
  const dir = fixture(t);
  rmSync(join(dir, 'conf/timers.app.toml'));
  let result = spawnSync('sh', [run, 'Release', 'timers', '1.0.1.1', dir], { encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /Missing configuration/);
  rmSync(join(dir, 'bin/app'));
  result = spawnSync('sh', [run, 'Release', 'timers', '1.0.1.1', dir], { encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /Missing executable/);
});

test('launch helper requires the renamed TOML preset without implicit XML fallback', (t) => {
  const dir = fixture(t);
  rmSync(join(dir, 'conf/timers.app.toml'));
  writeFileSync(join(dir, 'conf/timers.app.conf'), '<xml><plugins/></xml>');
  const result = spawnSync('sh', [run, 'Release', 'timers', '1.0.1.1', dir], { encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /Missing configuration: conf\/timers\.app\.toml/);
  assert.equal(result.stdout, '');
});

test('invalid arguments are rejected before starting or stopping a process', () => {
  for (const args of [[], ['Other', 'timers', '1.0.1.1'], ['Release', '../timers', '1.0.1.1']]) {
    assert.equal(spawnSync('sh', [run, ...args]).status, 2);
  }
  for (const args of [[], ['0'], ['1'], ['-1'], ['00123'], ['all'], ['123', '456']]) {
    assert.equal(spawnSync('sh', [stop, ...args]).status, 2);
  }
});

test('stop refuses another executable without sending a signal', (t) => {
  const dir = fixture(t);
  writeFileSync(join(dir, 'bin/ps'), '#!/bin/sh\nprintf "unrelated-service\\n"\n', { mode: 0o755 });
  const result = spawnSync('sh', [stop, '12345'], {
    env: { ...process.env, PATH: `${join(dir, 'bin')}:${process.env.PATH}` }, encoding: 'utf8',
  });
  assert.equal(result.status, 1);
  assert.match(result.stderr, /refusing to stop/);
});
