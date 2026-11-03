// Copyright (c) 2026 OpenArkStudio contributors
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdirSync, mkdtempSync, readFileSync, rmSync, symlinkSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { abiDiagnostics, parseAbiMetadata } from '../../.github/scripts/vcpkg-abi-diagnostics.mjs';

const script = fileURLToPath(new URL('../../.github/scripts/vcpkg-abi-diagnostics.mjs', import.meta.url));
const hash = value => createHash('sha256').update(value).digest('hex');
const safeFields = ['triplet', 'triplet_abi', 'features', 'cmake', 'powershell', 'compiler_hash',
  'portfile.cmake', 'ports.cmake', 'vcpkg.json', 'post_build_checks', 'sbom_info',
  'vcpkg-cmake', 'vcpkg-cmake-config', 'abseil', 'utf8-range'];
// Record structure confirmed against installed Asio, spdlog and Protobuf metadata.
// Values are synthetic; raw local ABI files are not copied into the repository.
const metadata = 'cmake 4.4.4\nfeatures core\nportfile.cmake recipe-hash\nports.cmake helper-hash\n' +
  'post_build_checks 2\ntriplet x64-windows\ntriplet_abi triplet-toolchain-compiler-hashes\n' +
  'vcpkg.json manifest-hash\n';

function fixture(t) {
  const root = mkdtempSync(join(tmpdir(), 'ark-abi-diagnostics-'));
  t.after(() => rmSync(root, { recursive: true, force: true }));
  const file = port => join(root, 'ci-build', 'vcpkg_installed', 'x64-windows', 'share', port, 'vcpkg_abi_info.txt');
  const write = (port, contents = metadata) => {
    mkdirSync(dirname(file(port)), { recursive: true });
    writeFileSync(file(port), contents);
  };
  for (const port of ['asio', 'spdlog', 'protobuf']) write(port);
  return { root, file, write };
}

function parse(text) {
  return parseAbiMetadata(Buffer.from(text));
}

test('known records expose only field-value SHA256 and the exact original-file package ABI', () => {
  const bytes = Buffer.from(metadata + 'private/path/key secret-value\n');
  const report = parseAbiMetadata(bytes);
  assert.equal(report.status, 'available');
  assert.equal(report.packageAbi, hash(bytes));
  assert.deepEqual(Object.keys(report.fields), safeFields);
  assert.deepEqual(report.fields.cmake, { status: 'available', sha256: hash('4.4.4') });
  assert.deepEqual(report.fields.triplet_abi, { status: 'available', sha256: hash('triplet-toolchain-compiler-hashes') });
  assert.deepEqual(report.fields.compiler_hash, { status: 'unavailable', reason: 'missing-field' });
  assert.doesNotMatch(JSON.stringify(report), /private\/path|secret-value|recipe-hash|4\.4\.4/);
});

test('CRLF parsing keeps field fingerprints stable without normalizing the package ABI bytes', () => {
  const lf = parse(metadata);
  const crlfBytes = Buffer.from(metadata.replaceAll('\n', '\r\n'));
  const crlf = parseAbiMetadata(crlfBytes);
  assert.equal(crlf.status, 'available');
  assert.deepEqual(crlf.fields, lf.fields);
  assert.equal(crlf.packageAbi, hash(crlfBytes));
  assert.notEqual(crlf.packageAbi, lf.packageAbi);
  const spaces = parse(metadata.replace('cmake 4.4.4', 'cmake value with spaces  '));
  assert.equal(spaces.fields.cmake.sha256, hash('value with spaces  '));
});

test('missing optional fields stay unavailable, while incomplete or ambiguous records stay closed', () => {
  for (const text of ['', metadata.trimEnd(), metadata.replace('features core\n', ''),
    metadata + 'triplet x64-windows\n', metadata + 'bad-record\n', metadata + '\n',
    metadata.replace('features core', 'features \u0000private'), metadata.replace('core', 'core\tprivate')]) {
    assert.deepEqual(parse(text), { status: 'unavailable', reason: 'malformed-metadata' });
  }
  assert.deepEqual(parse(metadata.replace('x64-windows', 'arm64-windows')),
    { status: 'unavailable', reason: 'unexpected-triplet' });
  assert.deepEqual(parseAbiMetadata(Buffer.from([0xff, 0xfe])),
    { status: 'unavailable', reason: 'malformed-metadata' });
  const optionalEmpty = parse(metadata + 'powershell \n');
  assert.deepEqual(optionalEmpty.fields.powershell, { status: 'unavailable', reason: 'missing-field' });
});

test('each metadata file has a strict 64 KiB bound, including the parser entry point', t => {
  const f = fixture(t);
  const exact = metadata + 'unknown ' + 'x'.repeat(64 * 1024 - Buffer.byteLength(metadata) - 9) + '\n';
  assert.equal(Buffer.byteLength(exact), 64 * 1024);
  assert.equal(parse(exact).status, 'available');
  assert.deepEqual(parse(exact + '\n'), { status: 'unavailable', reason: 'oversized-metadata' });
  f.write('asio', exact);
  f.write('spdlog', exact + '\n');
  const report = abiDiagnostics(f);
  assert.equal(report.packages[0].status, 'available');
  assert.deepEqual(report.packages[1], { port: 'spdlog', status: 'unavailable', reason: 'oversized-metadata' });
  assert.equal(report.packages[2].status, 'available');
});

test('only the three fixed installed paths are read, with independent unavailable states', t => {
  const f = fixture(t);
  const before = readFileSync(f.file('asio'));
  rmSync(f.file('spdlog'));
  f.write('protobuf', 'unsupported-format\n');
  writeFileSync(join(dirname(f.file('spdlog')), 'x64-windows.vcpkg_abi_info.txt'), metadata);
  const report = abiDiagnostics(f);
  assert.equal(report.schema, 1);
  assert.equal(report.kind, 'vcpkg-abi-diagnostics');
  assert.equal(report.triplet, 'x64-windows');
  assert.deepEqual(report.packages.map(p => [p.port, p.status]),
    [['asio', 'available'], ['spdlog', 'unavailable'], ['protobuf', 'unavailable']]);
  assert.equal(report.packages[1].reason, 'missing-metadata');
  assert.equal(report.packages[2].reason, 'malformed-metadata');
  assert.deepEqual(readFileSync(f.file('asio')), before, 'diagnostics never rewrite metadata');
});

test('missing directories and non-regular metadata return fixed unavailable reasons', t => {
  const f = fixture(t);
  rmSync(f.file('asio'));
  mkdirSync(f.file('asio'));
  assert.equal(abiDiagnostics(f).packages[0].reason, 'unsafe-path');
  assert.ok(abiDiagnostics({ root: join(f.root, 'not-present') }).packages.every(p => p.reason === 'missing-metadata'));
  assert.ok(abiDiagnostics({ root: f.file('spdlog') }).packages.every(p => p.reason === 'unsafe-path'));
});

test('metadata and ancestor symlinks never lead outside the fixed installed directory', { skip: process.platform === 'win32' }, t => {
  const f = fixture(t);
  const secret = join(f.root, 'outside-secret');
  writeFileSync(secret, metadata);
  rmSync(f.file('asio'));
  symlinkSync(secret, f.file('asio'));
  assert.equal(abiDiagnostics(f).packages[0].reason, 'unsafe-path');
  rmSync(dirname(f.file('spdlog')), { recursive: true });
  symlinkSync(dirname(f.file('protobuf')), dirname(f.file('spdlog')));
  assert.equal(abiDiagnostics(f).packages[1].reason, 'unsafe-path');
  const alias = join(f.root, 'root-alias');
  symlinkSync(f.root, alias);
  assert.ok(abiDiagnostics({ root: alias }).packages.every(p => p.reason === 'unsafe-path'));
  rmSync(join(f.root, 'ci-build'), { recursive: true });
  symlinkSync(f.root, join(f.root, 'ci-build'));
  assert.ok(abiDiagnostics(f).packages.every(p => p.reason === 'unsafe-path'));
});

test('CLI output never includes raw values, unknown field names, paths or inherited environment', t => {
  const f = fixture(t);
  const secret = 'raw-private-token-do-not-emit';
  f.write('asio', metadata.replace('recipe-hash', secret) + `${secret} ${f.root}\n`);
  f.write('protobuf', 'unparseable-private-text\n');
  const child = spawnSync(process.execPath, [script], {
    cwd: f.root, encoding: 'utf8', env: { ...process.env, VCPKG_ROOT: secret, CXXFLAGS: secret, UNRELATED_SECRET: secret },
  });
  assert.equal(child.status, 0);
  assert.equal(child.stderr, '');
  const report = JSON.parse(child.stdout);
  assert.equal(report.packages[0].fields['portfile.cmake'].sha256, hash(secret));
  assert.equal(report.packages[2].status, 'unavailable');
  for (const forbidden of [secret, f.root, 'unparseable-private-text', 'recipe-hash', 'triplet-toolchain-compiler-hashes']) {
    assert.equal(child.stdout.includes(forbidden), false, forbidden);
  }
  assert.ok(child.stdout.length < 10_000);
  assert.doesNotMatch(child.stdout, /cache-hit|cache restored|speedup|VCPKG_ROOT|CXXFLAGS/);
});

test('only Windows Applications reports metadata after configure/save, without changing native failure rules', () => {
  const workflow = readFileSync(new URL('../../.github/workflows/ci.yml', import.meta.url), 'utf8');
  const jobs = workflow.split(/(?=^  [a-z][a-z0-9_-]*:$)/m);
  const app = jobs.find(block => block.startsWith('  applications:\n'));
  const steps = app.split(/(?=^      - )/m).slice(1);
  const diagnostic = steps.filter(step => step.includes('vcpkg-abi-diagnostics.mjs'));
  assert.equal(diagnostic.length, 1);
  assert.equal(workflow.split('run: node .github/scripts/vcpkg-abi-diagnostics.mjs').length - 1, 1);
  assert.match(diagnostic[0], /^        if: \$\{\{ !cancelled\(\) && runner\.os == 'Windows' \}\}$/m);
  assert.match(diagnostic[0], /^        continue-on-error: true$/m);
  assert.doesNotMatch(diagnostic[0], /upload-artifact|GITHUB_ENV|GITHUB_OUTPUT|VCPKG_BINARY|VCPKG_DISABLE_COMPILER_TRACKING/);
  const position = steps.indexOf(diagnostic[0]);
  assert.match(steps[position - 1], /uses: actions\/cache\/save@v4/);
  assert.match(steps[position - 2], /name: Configure applications/);
  for (const step of steps.filter(step => /name: Configure applications|run: cmake --build/.test(step))) {
    assert.doesNotMatch(step, /continue-on-error:|if:/, 'configuration/build/test failure still fails the job');
  }
});
