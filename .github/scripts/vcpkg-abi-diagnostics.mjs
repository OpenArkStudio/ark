// Copyright (c) 2026 OpenArkStudio contributors
import { createHash } from 'node:crypto';
import { closeSync, constants, fstatSync, lstatSync, openSync, readSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const ports = ['asio', 'spdlog', 'protobuf'];
const triplet = 'x64-windows';
const maxBytes = 64 * 1024;
// Names are fixed: unknown metadata keys can themselves contain paths or secrets.
const fields = ['triplet', 'triplet_abi', 'features', 'cmake', 'powershell', 'compiler_hash',
  'portfile.cmake', 'ports.cmake', 'vcpkg.json', 'post_build_checks', 'sbom_info',
  'vcpkg-cmake', 'vcpkg-cmake-config', 'abseil', 'utf8-range'];
const sha256 = value => createHash('sha256').update(value).digest('hex');
const unavailable = reason => ({ status: 'unavailable', reason });

export function parseAbiMetadata(bytes) {
  if (!Buffer.isBuffer(bytes) || bytes.length === 0) return unavailable('malformed-metadata');
  if (bytes.length > maxBytes) return unavailable('oversized-metadata');
  let text;
  try {
    text = new TextDecoder('utf-8', { fatal: true }).decode(bytes);
  } catch {
    return unavailable('malformed-metadata');
  }
  // vcpkg writes newline-terminated key/value records, separated by one space.
  // Keep the original bytes for the package ABI; CRLF normalization is for parsing only.
  text = text.replaceAll('\r\n', '\n');
  if (!text.endsWith('\n') || /[\x00-\x09\x0b-\x1f\x7f]/.test(text)) return unavailable('malformed-metadata');
  const values = new Map();
  for (const line of text.slice(0, -1).split('\n')) {
    const delimiter = line.indexOf(' ');
    if (delimiter <= 0) return unavailable('malformed-metadata');
    const name = line.slice(0, delimiter);
    if (!fields.includes(name)) continue;
    if (values.has(name)) return unavailable('malformed-metadata');
    values.set(name, line.slice(delimiter + 1));
  }
  if (!['triplet', 'triplet_abi', 'features'].every(name => values.get(name)?.trim())) {
    return unavailable('malformed-metadata');
  }
  if (values.get('triplet') !== triplet) return unavailable('unexpected-triplet');
  return {
    status: 'available', packageAbi: sha256(bytes),
    fields: Object.fromEntries(fields.map(name => [name, values.get(name)?.trim()
      ? { status: 'available', sha256: sha256(values.get(name)) } : unavailable('missing-field')])),
  };
}

function readMetadata(root, port) {
  let descriptor;
  try {
    let file = resolve(root);
    if (!lstatSync(file).isDirectory()) return unavailable('unsafe-path');
    const components = ['ci-build', 'vcpkg_installed', triplet, 'share', port, 'vcpkg_abi_info.txt'];
    let expectedFile;
    for (const [index, component] of components.entries()) {
      file = resolve(file, component);
      const stat = lstatSync(file);
      if (stat.isSymbolicLink() || (index < components.length - 1 ? !stat.isDirectory() : !stat.isFile())) {
        return unavailable('unsafe-path');
      }
      expectedFile = stat;
    }
    descriptor = openSync(file, constants.O_RDONLY | (constants.O_NOFOLLOW ?? 0));
    const stat = fstatSync(descriptor);
    if (!stat.isFile() || stat.dev !== expectedFile.dev || stat.ino !== expectedFile.ino) return unavailable('unsafe-path');
    if (stat.size > maxBytes) return unavailable('oversized-metadata');
    // Bound the actual read as well as stat.size, including a file that grows while read.
    const bytes = Buffer.alloc(maxBytes + 1);
    let length = 0;
    while (length < bytes.length) {
      const count = readSync(descriptor, bytes, length, bytes.length - length, length);
      if (count === 0) break;
      length += count;
    }
    return parseAbiMetadata(bytes.subarray(0, length));
  } catch (error) {
    // Never print exception messages: they may contain raw paths or file contents.
    return unavailable(error?.code === 'ENOENT' ? 'missing-metadata' : 'unreadable-metadata');
  } finally {
    if (descriptor !== undefined) closeSync(descriptor);
  }
}

export function abiDiagnostics({ root }) {
  return {
    schema: 1, kind: 'vcpkg-abi-diagnostics', triplet,
    packages: ports.map(port => ({ port, ...readMetadata(root, port) })),
  };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  console.log(JSON.stringify(abiDiagnostics({ root: process.cwd() })));
}
