import { accessSync, constants, readdirSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

export function requireArchives(env) {
  const directory = env.VCPKG_ARCHIVE_DIRECTORY;
  if (env.VCPKG_ARCHIVE_ENABLED !== 'true' || !directory ||
      !/^ark-vcpkg-files-v2-Windows-(?:X64|ARM64|ARM)-[a-f0-9]{64}-[a-f0-9]{64}$/.test(env.VCPKG_ARCHIVE_KEY ?? '')) {
    throw new Error('Dependency seed requires an enabled files provider and a complete archive key');
  }
  accessSync(directory, constants.R_OK | constants.W_OK);
  const archives = readdirSync(directory, { recursive: true, withFileTypes: true })
    .filter(entry => entry.isFile() && entry.name.endsWith('.zip')).length;
  if (!archives) throw new Error('Dependency seed requires nonempty package archives');
  return archives;
}

export function requireAvailableSeed(env) {
  if (env.VCPKG_ARCHIVE_HIT !== 'true' && env.VCPKG_VERIFIED_HIT !== 'true') {
    throw new Error('Dependency seed requires a restored or verified exact cache entry');
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  if (process.argv.includes('--available')) {
    requireAvailableSeed(process.env);
    console.log('Exact dependency cache entry is available on main');
  } else {
    console.log(`Dependency seed archives ready: ${requireArchives(process.env)}`);
  }
}
