import { execFileSync } from 'node:child_process';
import { appendFileSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { changedPaths } from './ci-policy.mjs';

export function compilerSeedPlan({ env, event, latestMain, files }) {
  if (env.GITHUB_REF !== 'refs/heads/main' || !['push', 'workflow_dispatch'].includes(env.GITHUB_EVENT_NAME)) {
    return { build: false, reason: 'trusted-main-only' };
  }
  if (env.GITHUB_EVENT_NAME === 'workflow_dispatch') return { build: true, reason: 'manual-main-seed' };
  if (latestMain && latestMain !== env.GITHUB_SHA) return { build: false, reason: 'newer-main-available' };
  if (!Array.isArray(files) || files.length === 0) return { build: true, reason: 'unknown-inputs' };
  const native = files.some(file => /\.(?:c|cc|cpp|cxx|h|hpp|inl|ipp|tpp|proto|cmake)$/i.test(file) ||
    /(?:^|\/)CMakeLists\.txt$/.test(file) || /^(?:ports\/|vcpkg(?:-configuration)?\.json$)/.test(file) ||
    /^\.github\/(?:workflows|scripts)\//.test(file));
  return { build: native, reason: native ? 'accepted-native-inputs' : 'no-compiler-inputs' };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    const env = process.env;
    const event = JSON.parse(readFileSync(env.GITHUB_EVENT_PATH, 'utf8'));
    let files, latestMain;
    if (env.GITHUB_EVENT_NAME === 'push') {
      // Fetching records the current accepted main without changing the checkout.
      // Cache-only work may skip superseded inputs; acceptance jobs never do.
      execFileSync('git', ['fetch', '--no-tags', 'origin', 'main'], { stdio: 'pipe' });
      latestMain = execFileSync('git', ['rev-parse', 'FETCH_HEAD'], { encoding: 'utf8' }).trim();
      try {
        files = changedPaths({ root: process.cwd(), base: event.before, head: event.after, pullRequest: false });
      } catch {
        // Unknown changes warm accepted code conservatively.
      }
    }
    const plan = compilerSeedPlan({ env, event, latestMain, files });
    appendFileSync(env.GITHUB_OUTPUT, `build=${plan.build}\nreason=${plan.reason}\n`);
    console.log(`Compiler seed: ${plan.reason}`);
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
