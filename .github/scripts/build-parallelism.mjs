import { appendFileSync } from 'node:fs';
import { availableParallelism, totalmem } from 'node:os';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const gibibyte = 1024 ** 3;
const maxBuildJobs = 4;

function positiveInteger(value) {
  return Number.isSafeInteger(value) && value > 0 ? value : null;
}

function detected(read) {
  try {
    return positiveInteger(read());
  } catch {
    return null;
  }
}

export function detectBuildResources({
  readCpuCount = availableParallelism,
  readTotalMemory = totalmem,
  readConstrainedMemory = () => process.constrainedMemory(),
} = {}) {
  const memories = [detected(readTotalMemory), detected(readConstrainedMemory)].filter(value => value !== null);
  return {
    cpuCount: detected(readCpuCount),
    // constrainedMemory() is zero when there is no known process/cgroup limit.
    memoryBytes: memories.length ? Math.min(...memories) : null,
  };
}

function applyOverride(value, maximum, fallback, limit) {
  if (value === undefined || value === '') return maximum;
  return typeof value === 'string' && new RegExp(`^[1-${limit}]$`).test(value)
    ? Math.min(maximum, Number(value)) : Math.min(maximum, fallback);
}

export function buildParallelismPlan({ cpuCount, memoryBytes, buildJobsOverride, linkJobsOverride } = {}) {
  const cpu = positiveInteger(cpuCount);
  const memory = positiveInteger(memoryBytes);
  // Initial CI budget: reserve 1 GiB and allow 3 GiB per compiler process.
  // Keep the existing two-job ceiling when either resource is unknown. This
  // leaves 7-8 GiB runners at two jobs and never exceeds four on larger hosts.
  // These are initial scheduling estimates, not peak-memory guarantees.
  const memoryJobs = memory === null ? 2 : Math.max(1, Math.floor((memory - gibibyte) / (3 * gibibyte)));
  const resourceJobs = Math.min(maxBuildJobs, cpu ?? 2, memoryJobs);
  const buildJobs = applyOverride(buildJobsOverride, resourceJobs, 2, maxBuildJobs);
  // Two six-GiB links can overlap with two three-GiB compilers at four jobs:
  // reserve 1 + 2*6 + 2*3 = 19 GiB before admitting both links. Smaller hosts
  // retain a single link alongside the existing compile-job heuristic.
  const twoLinkMemory = (1 + 2 * 6 + (maxBuildJobs - 2) * 3) * gibibyte;
  const resourceLinks = buildJobs === maxBuildJobs && memory !== null && memory >= twoLinkMemory ? 2 : 1;
  const linkJobs = applyOverride(linkJobsOverride, resourceLinks, 1, 2);
  return { cpuCount: cpu, memoryBytes: memory, buildJobs, linkJobs,
    jobPools: `ark_compile=${buildJobs};ark_link=${linkJobs}` };
}

export function exportBuildParallelism({ env, resources = detectBuildResources() }) {
  if (!env.GITHUB_ENV || !env.GITHUB_OUTPUT) throw new Error('GitHub file-command channels are required');
  const plan = buildParallelismPlan({ ...resources,
    buildJobsOverride: env.ARK_BUILD_JOBS_OVERRIDE, linkJobsOverride: env.ARK_LINK_JOBS_OVERRIDE });
  appendFileSync(env.GITHUB_ENV, `ARK_BUILD_JOBS=${plan.buildJobs}\nARK_LINK_JOBS=${plan.linkJobs}\nARK_CMAKE_JOB_POOLS=${plan.jobPools}\n`);
  appendFileSync(env.GITHUB_OUTPUT, `build_jobs=${plan.buildJobs}\nlink_jobs=${plan.linkJobs}\njob_pools=${plan.jobPools}\ncpu_count=${plan.cpuCount ?? 'unknown'}\nmemory_bytes=${plan.memoryBytes ?? 'unknown'}\n`);
  return plan;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    const plan = exportBuildParallelism({ env: process.env });
    const memory = plan.memoryBytes === null ? 'unknown' : `${(plan.memoryBytes / gibibyte).toFixed(2)} GiB`;
    console.log(`Build resources: CPUs=${plan.cpuCount ?? 'unknown'}, memory=${memory}, compile jobs=${plan.buildJobs}, link jobs=${plan.linkJobs}`);
  } catch (error) {
    console.error(`Unable to export build parallelism: ${error.message}`);
    process.exitCode = 1;
  }
}
