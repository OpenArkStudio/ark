import { execFileSync } from 'node:child_process';
import { appendFileSync, existsSync, readFileSync, statSync } from 'node:fs';
import { posix, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { applicationTargetsForPath, selectApplicationBuildPlan } from './application-build-plan.mjs';

const shaPattern = /^(?:[a-f0-9]{40}|[a-f0-9]{64})$/;
const cppPath = /\.(?:c|cc|cpp|cxx|c\+\+|h|hh|hpp|hxx|inl|ipp|tpp|proto)$/i;
const docPath = /\.(?:md|mdx|markdown|rst|adoc|txt|html|css|js|mjs|cjs|ts|tsx|jsx|astro|json|yaml|yml|svg|png|jpe?g|gif|webp|ico|pdf|woff2?|ttf|otf)$/i;
const dependencyName = /^(?:package(?:-lock)?\.json|npm-shrinkwrap\.json|pnpm-lock\.yaml|yarn\.lock|vcpkg(?:-configuration)?\.json|conanfile\.(?:py|txt)|conan\.lock|requirements(?:-[\w.-]+)?\.txt|.*\.lock|\.gitmodules)$/i;
const pluginIndexPaths = new Set(['src/plugins/README.md', 'src/plugins/README_CN.md']);
// Reviewed static test manuals only; unknown Markdown and executable inputs stay conservative.
const testGuidePaths = new Set(['tests/core/README.md', 'tests/core/README_CN.md',
  'tests/database/README.md', 'tests/database/README_CN.md',
  'tests/database/cancellation.md', 'tests/database/cancellation_CN.md']);

function isRepositoryPath(file) {
  return typeof file === 'string' && file.length > 0 && !/[\x00-\x1f\x7f\\]/.test(file) &&
    !posix.isAbsolute(file) && posix.normalize(file) === file && !file.split('/').includes('..');
}

export function isDocumentationPath(file) {
  if (!isRepositoryPath(file)) return false;
  const name = posix.basename(file);
  if (cppPath.test(file) || /^CMake(?:Lists\.txt|(?:User)?Presets\.json|Cache\.txt)$/i.test(name) || /\.cmake$/i.test(file) ||
      dependencyName.test(name) || file.split('/').includes('.github') || /^action\.ya?ml$/i.test(name) ||
      /^\.clang-(?:format|tidy)$/.test(name)) return false;
  if (file === 'AGENTS.md' || file === 'README.md' || file === 'README_CN.md' || pluginIndexPaths.has(file) || testGuidePaths.has(file)) return true;
  return file.startsWith('docs/') && (docPath.test(file) || name === 'LICENSE' || name === '.gitignore');
}

export function decodePaths(buffer) {
  const text = new TextDecoder('utf-8', { fatal: true }).decode(buffer);
  if (text && !text.endsWith('\0')) throw new Error('truncated Git path list');
  const files = text ? text.slice(0, -1).split('\0') : [];
  if (files.some(file => !file) || new Set(files).size !== files.length) throw new Error('invalid Git path list');
  return files;
}

export function changedPaths({ root, base, head, pullRequest = true }, runGit) {
  if (typeof base !== 'string' || typeof head !== 'string' || !shaPattern.test(base) || !shaPattern.test(head) ||
      /^0+$/.test(base) || /^0+$/.test(head)) {
    throw new Error('missing or invalid commit identity');
  }
  const git = runGit ?? (args => execFileSync('git', args, {
    cwd: root, maxBuffer: 32 * 1024 * 1024, timeout: 60_000, stdio: ['ignore', 'pipe', 'pipe'],
  }));
  if (git(['rev-parse', '--is-shallow-repository']).toString().trim() !== 'false') {
    throw new Error('incomplete Git history');
  }
  git(['cat-file', '-e', `${base}^{commit}`]);
  git(['cat-file', '-e', `${head}^{commit}`]);
  let start = base;
  if (pullRequest) {
    const ancestors = git(['merge-base', '--all', base, head]).toString().trim().split(/\s+/);
    if (ancestors.length !== 1 || !shaPattern.test(ancestors[0])) throw new Error('ambiguous merge base');
    start = ancestors[0];
  }
  // Renames become deletion + addition, so a move into docs cannot hide old code.
  return decodePaths(git(['diff', '--no-ext-diff', '--no-textconv', '--no-renames', '--name-only', '-z', start, head, '--']));
}

export function inspectSite(root) {
  const site = resolve(root, 'docs/site');
  const present = existsSync(site);
  let available = false;
  try {
    const manifest = JSON.parse(readFileSync(resolve(site, 'package.json'), 'utf8'));
    available = statSync(resolve(site, 'package-lock.json')).isFile() &&
      typeof manifest.scripts?.check === 'string' && manifest.scripts.check.trim().length > 0;
  } catch {
    // A missing legacy site forces full C++; an introduced/broken site must fail its own job.
  }
  return { present, available };
}

const nativeJobs = ['network', 'applications', 'database'];

// Shared SDK/build inputs affect every consumer. Only known module paths narrow PRs.
function modulesForPath(file) {
  if (isDocumentationPath(file)) return [];
  if (!isRepositoryPath(file)) return nativeJobs;
  if (applicationTargetsForPath(file)) return ['applications'];
  if (!cppPath.test(file) && !file.endsWith('/CMakeLists.txt')) return nativeJobs;
  if (/^(?:tests\/network\/|benchmarks\/network_buffers\.cpp$)/.test(file)) return ['network'];
  if (/^(?:tests\/database\/|benchmarks\/database\.cpp$)/.test(file)) return ['database'];
  if (/^(?:include\/ark\/|src\/plugins\/)(?:net|http|bus)\//.test(file)) return ['network', 'applications'];
  if (/^(?:include\/ark\/|src\/plugins\/)(?:mysql|redis)\//.test(file) ||
      /^examples\/(?:mysql_sample|redis_sample)\//.test(file)) return ['applications', 'database'];
  if (/^src\/plugins\/detail\//.test(file)) return ['applications', 'database'];
  if (/^(?:application\/|src\/(?:application|server|config|proto)\/|tests\/(?:core|app|consumer)\/|examples\/)/.test(file) ||
      /^(?:include\/ark\/|src\/plugins\/)(?:kernel|utility|consul|etcd_client|kafka|osssdk|physic)\//.test(file)) {
    return ['applications'];
  }
  return nativeJobs;
}

export function selectScope({ eventName, files, site, enumerationFailed = false }) {
  const known = !enumerationFailed && Array.isArray(files) && files.every(isRepositoryPath);
  const paths = known ? files : [];
  const nonempty = known && paths.length > 0;
  const scopedEvent = eventName === 'pull_request' || eventName === 'merge_group';
  const docsOnly = scopedEvent && nonempty && site.available && paths.every(isDocumentationPath);
  const readme = paths.some(file => file === 'README.md' || file === 'README_CN.md');
  const docsTouched = readme || paths.some(file => file === 'AGENTS.md' || file.startsWith('docs/') || pluginIndexPaths.has(file) || testGuidePaths.has(file));
  const ciTouched = paths.some(file => file.startsWith('.github/') || file.startsWith('tests/ci/'));
  const siteReferenced = site.present || paths.some(file => file.startsWith('docs/site/'));
  const docs = siteReferenced && (eventName !== 'pull_request' || !known || docsTouched || ciTouched);
  const native = !docsOnly;
  const selected = new Set();
  const narrow = native && eventName === 'pull_request' && nonempty && site.available;
  for (const file of paths) {
    for (const job of modulesForPath(file)) selected.add(job);
  }
  const unknown = narrow && paths.some(file => modulesForPath(file) === nativeJobs);
  if (native && (!narrow || unknown)) {
    for (const job of nativeJobs) selected.add(job);
  }
  const decision = {
    event_name: eventName,
    validation_stage: !native ? 'documentation' : narrow ? 'modules' : 'full',
    native_check: native,
    network_check: native && selected.has('network'),
    applications_check: native && selected.has('applications'),
    database_check: native && selected.has('database'),
    format_check: native,
    docs_check: docs,
    readme_check: readme,
    site_available: site.available,
    files_known: known,
    reason: docsOnly ? 'documentation-only' : !scopedEvent ? 'full-event' :
      !known ? 'enumeration-unavailable' : !nonempty ? 'empty-diff' :
        !site.available ? 'site-unavailable' : eventName === 'merge_group' ? 'code-build-or-unknown-path' :
          unknown ? 'unknown-path' : 'module-paths',
  };
  const applicationFiles = known ? paths.filter(file => modulesForPath(file).includes('applications')) : undefined;
  return { ...decision, app_plan_json: JSON.stringify(selectApplicationBuildPlan({ eventName, files: applicationFiles, scope: decision })) };
}

export function admissionState(eventName, event) {
  if (!['pull_request', 'merge_group', 'workflow_dispatch'].includes(eventName)) throw new Error('unsupported admission event');
  if (eventName !== 'pull_request') return 'ready';
  if (typeof event?.pull_request?.draft !== 'boolean') throw new Error('missing or invalid pull request draft metadata');
  return event.pull_request.draft ? 'deferred' : 'ready';
}

export class AdmissionDeferredError extends Error {
  constructor() {
    super('Draft admission deferred: native acceptance was not executed in this run; this PR is not admitted. The root scheduler must release it for fresh ready_for_review validation.');
    this.name = 'AdmissionDeferredError';
  }
}

export function validateGate(needs, eventName, admission = 'ready') {
  if (!['ready', 'deferred'].includes(admission)) throw new Error('invalid admission state');
  if (admission === 'deferred') {
    if (eventName !== 'pull_request') throw new Error('only draft pull requests may defer admission');
    throw new AdmissionDeferredError();
  }
  validateResults(needs, eventName, false);
}

export function validatePreview(needs, eventName, admission) {
  if (eventName !== 'pull_request' || admission !== 'deferred') throw new Error('preview requires a genuine draft pull request');
  validateResults(needs, eventName, true);
}

function validateResults(needs, eventName, preview) {
  if (!needs || needs.scope?.result !== 'success') throw new Error('scope job did not succeed');
  if (!['pull_request', 'merge_group', 'workflow_dispatch'].includes(eventName) ||
      needs.scope.outputs?.event_name !== eventName) throw new Error('scope event does not match this run');
  const flags = {};
  for (const key of ['native_check', 'network_check', 'applications_check', 'database_check', 'format_check',
    'docs_check', 'readme_check', 'site_available', 'files_known']) {
    const value = needs.scope.outputs?.[key];
    if (value !== 'true' && value !== 'false') throw new Error(`invalid scope output: ${key}`);
    flags[key] = value === 'true';
  }
  const reason = needs.scope.outputs.reason;
  const uncertain = !flags.files_known || !flags.site_available ||
    ['empty-diff', 'site-unavailable', 'enumeration-unavailable'].includes(reason);
  const stage = !flags.native_check ? 'documentation' : eventName === 'pull_request' && !uncertain ? 'modules' : 'full';
  if (needs.scope.outputs.validation_stage !== stage) throw new Error('invalid validation stage');
  if (!['documentation-only', 'full-event', 'enumeration-unavailable', 'empty-diff', 'site-unavailable',
    'code-build-or-unknown-path', 'unknown-path', 'module-paths'].includes(reason) ||
      (!flags.native_check && reason !== 'documentation-only') ||
      (flags.native_check && reason === 'documentation-only') || (stage === 'full' && reason === 'module-paths')) {
    throw new Error('invalid scope reason');
  }
  if (!flags.native_check && (!flags.docs_check || !flags.site_available || !flags.files_known ||
      nativeJobs.some(job => flags[`${job}_check`]))) throw new Error('documentation-only scope is incomplete');
  if (flags.format_check !== flags.native_check) throw new Error('native validation requires formatting');
  if (flags.native_check && !nativeJobs.some(job => flags[`${job}_check`])) throw new Error('native scope has no regressions');
  const fullModules = stage === 'full' || !flags.files_known ||
    ['unknown-path', 'empty-diff', 'site-unavailable', 'enumeration-unavailable'].includes(needs.scope.outputs.reason);
  if (flags.native_check && fullModules && nativeJobs.some(job => !flags[`${job}_check`])) {
    throw new Error('full or uncertain scope requires every native module');
  }
  if (eventName === 'workflow_dispatch' && !flags.native_check) throw new Error('manual validation must be full');
  if (eventName !== 'pull_request' && flags.site_available && !flags.docs_check) {
    throw new Error('queue and manual runs require the available documentation site');
  }
  const required = {
    network: flags.network_check, applications: flags.applications_check, database: flags.database_check,
  };
  for (const [job, mustRun] of Object.entries(required)) {
    const expected = !preview && mustRun ? 'success' : 'skipped';
    if (needs[job]?.result !== expected) throw new Error(`${job}: expected ${expected}, got ${needs[job]?.result ?? 'missing'}`);
  }
  if (needs.validation?.result !== 'success') throw new Error('validation job did not succeed');
  for (const [check, mustRun] of Object.entries({
    documentation: flags.docs_check, readme: flags.readme_check, formatting: flags.format_check,
  })) {
    const expected = mustRun ? 'success' : 'skipped';
    const outcome = needs.validation.outputs?.[`${check}_outcome`];
    if (outcome !== expected) throw new Error(`${check}: expected ${expected}, got ${outcome ?? 'missing'}`);
  }
}

function classify() {
  const root = process.cwd();
  const eventName = process.env.GITHUB_EVENT_NAME;
  let files;
  let admission = eventName === 'pull_request' ? 'unknown' : 'ready';
  let enumerationFailed = false;
  try {
    const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
    if (eventName === 'pull_request') {
      admission = admissionState(eventName, event);
      files = changedPaths({ root, base: event.pull_request?.base?.sha, head: event.pull_request?.head?.sha });
    } else if (eventName === 'merge_group') {
      files = changedPaths({ root, base: event.merge_group?.base_sha, head: event.merge_group?.head_sha, pullRequest: false });
    } else if (eventName === 'push') {
      files = changedPaths({ root, base: event.before, head: event.after, pullRequest: false });
    } else {
      enumerationFailed = true;
    }
  } catch {
    enumerationFailed = true;
  }
  const decision = selectScope({ eventName, files, site: inspectSite(root), enumerationFailed });
  const output = Object.entries(decision).map(([key, value]) => `${key}=${value}\n`).join('');
  if (process.env.GITHUB_OUTPUT) appendFileSync(process.env.GITHUB_OUTPUT, output);
  const summary = `CI scope: ${decision.reason}\n\n${output}\n` +
    (admission === 'deferred' ? 'Admission deferred: native checks will not execute. Light checks do not admit this draft or establish native acceptance.\n' :
      admission === 'unknown' ? 'Invalid draft metadata: full scope fallback; the required gate must reject admission.\n' : '') +
    (!decision.site_available ? (decision.docs_check ? 'The required Nimbus site is missing or invalid; its job must fail.\n' :
      'Nimbus is not introduced in this checkout; site validation is explicitly not required and all native modules remain required.\n') : '');
  if (process.env.GITHUB_STEP_SUMMARY) appendFileSync(process.env.GITHUB_STEP_SUMMARY, summary);
  console.log(JSON.stringify({ ...decision, admission, file_count: files?.length ?? null }, null, 2));
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    if (process.argv[2] === 'classify') classify();
    else if (['gate', 'preview'].includes(process.argv[2])) {
      const event = JSON.parse(readFileSync(process.env.GITHUB_EVENT_PATH, 'utf8'));
      const admission = admissionState(process.env.GITHUB_EVENT_NAME, event);
      const needs = JSON.parse(process.env.CI_NEEDS_JSON);
      if (process.argv[2] === 'preview') {
        validatePreview(needs, process.env.GITHUB_EVENT_NAME, admission);
        const summary = 'Draft preview only: scope and selected light checks succeeded. Native acceptance was not executed in this run; this PR is not admitted. This preview does not report CI - required success. Root scheduling must release it for fresh ready_for_review checks before admission.\n';
        if (process.env.GITHUB_STEP_SUMMARY) appendFileSync(process.env.GITHUB_STEP_SUMMARY, summary);
        console.log(summary.trim());
      } else {
        validateGate(needs, process.env.GITHUB_EVENT_NAME, admission);
        console.log('Every required job and validation step succeeded; only explicitly unnecessary checks were skipped.');
      }
    } else if (process.argv[2] === 'require-site') {
      if (!inspectSite(process.cwd()).available) throw new Error('Required Nimbus package, lockfile or check script is unavailable');
    } else throw new Error('Expected classify, gate, preview or require-site');
  } catch (error) {
    if (error instanceof AdmissionDeferredError && process.env.GITHUB_STEP_SUMMARY) {
      appendFileSync(process.env.GITHUB_STEP_SUMMARY,
        'Admission deferred (not a passing check). Native acceptance was not executed in this run; this draft is not admitted. Light-check results remain separate. Root scheduling must release it for fresh ready_for_review checks.\n');
    }
    console.error(`${error instanceof AdmissionDeferredError ? '[DRAFT_ADMISSION_DEFERRED] ' : ''}${error.message}`);
    process.exitCode = 1;
  }
}
