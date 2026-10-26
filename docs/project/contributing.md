# Contributing and documentation

Changes target the protected main branch through pull requests. Run the relevant checks and squash merge with an English commit subject. See the [contribution guide](https://github.com/OpenArkStudio/ark/blob/main/.github/CONTRIBUTING.md).

## Develop the documentation

The site uses Cloudflare Nimbus with Astro. Markdown in `docs/` is the content source; each page has an English file and a `_CN.md` translation. Generated site content and output are ignored.

```sh
npm ci --prefix docs/site
npm run dev --prefix docs/site -- --host 127.0.0.1
npm run check --prefix docs/site
```

The site provides language switching, search, code highlighting and plain Markdown endpoints for AI readers. Pull requests run the documentation checks without publishing. After a change is merged into `main`, the documentation workflow builds and publishes GitHub Pages; maintainers can also trigger it manually. Node.js 22.23.4+ is needed for the documentation build; it is not needed to compile ark or its C++ tests.


## CI documentation scope

The exact root `AGENTS.md` file is documentation. When the changed range is known,
nonempty and confined to recognized documentation, and the Nimbus site is available,
both source PRs and merge-group candidates require the documentation checks and
`CI - required`, while native jobs are explicitly skipped. Nimbus checks the site;
root README link checks run only when a root README changes.

Nested unknown `AGENTS.md` paths, similarly named code, mixed source changes,
unavailable ranges or an unavailable site retain native checks. Manual runs keep
the full native matrix. Changes to the CI policy itself still require native
acceptance; this documentation exception does not change that requirement.

## CI dependency archive cache

CI restores vcpkg package archives from the runner temporary directory with
`actions/cache`. A versioned v2 key contains a compatibility prefix and an exact
runner-image suffix. The prefix includes the runner OS and architecture,
`ImageOS`, pinned vcpkg revision, manifest and configuration bytes (including the
baseline), local port overlay, selected/default features, build profile, compiler
environment flags and target/host triplet overrides. Only `ImageVersion` is moved
to the suffix. Within each ref searched by Actions, lookup uses the exact key
first, then that single prefix; there is no OS-only or cross-profile fallback.
Both image fields remain required.

An archive restore only makes candidates available to the vcpkg `files` provider.
It does not install an existing dependency tree. vcpkg computes each requested
package's full ABI hash, including compiler tracking, and retrieves only matching
archives; mismatched packages build from source. The workflow does not disable
compiler tracking, override ABI hashes or rename package archives. See the
[vcpkg ABI mismatch guide](https://learn.microsoft.com/en-us/vcpkg/users/binarycaching-troubleshooting#cause-3-differences-between-local-and-remote-build-environments).
A rolling image can therefore share an archive cache without treating its
packages as compatible in advance.

After successful configuration, a miss or prefix restore saves only the exact
key. There is no second save under the compatibility prefix. Image-specific
snapshots can still overlap and use cache capacity. The v1 keys cannot be safely
searched by this prefix and are deliberately excluded: v2 starts cold until an
accessible run has saved a v2 cache for the same dependency configuration.
Actions cache branch/ref visibility still applies; see the [Actions cache reference](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#cache-key-matching).
This change does not make caches from another merge-queue ref readable. A compatible v2 archive on the default branch is needed to seed queue
runs that cannot access each other's entries.

Missing identity, unsupported dependency layout or archive paths keep persistence
disabled with `VCPKG_BINARY_SOURCES=clear`. Cache-service failures retain ordinary
source configuration, compilation and regressions. The cache covers archives only,
not installed trees. Project object caching is configured separately below. Local Node tests validate the
lookup boundaries, exports and workflow wiring; hosted cache reuse and any timing
improvement require separate CI evidence.


### Windows ABI diagnostics

After dependency configuration and the existing archive-save step, Windows
Applications logs a bounded JSON report for Asio, rapidxml and Protobuf. The
helper reads only `ci-build/vcpkg_installed/x64-windows/share/<port>/vcpkg_abi_info.txt`,
without searching directories or invoking build tools. It rejects symbolic links,
non-regular files, malformed records, unexpected triplets and files over 64 KiB.
Absent or rejected metadata is explicitly `unavailable`; missing optional fields
also remain `unavailable` rather than being treated as equal.

`packageAbi` is the SHA256 of the exact ABI metadata bytes. Field values are
reported only as SHA256 fingerprints under fixed names: `triplet`, `triplet_abi`,
`features`, `cmake`, `powershell`, `compiler_hash`, `portfile.cmake`, `ports.cmake`,
`vcpkg.json`, `post_build_checks`, `sbom_info`, `vcpkg-cmake`, `vcpkg-cmake-config`,
`abseil` and `utf8-range`. Unknown field names and all raw values are omitted;
no full environment, absolute paths or credentials are logged. Metadata files
and artifacts are not uploaded.
A separate `compiler_hash` record may be absent; the report does not infer one
from a compiler version string or split the opaque `triplet_abi` value.

The diagnostic also runs after an earlier step fails, unless the job is cancelled.
Its availability status describes metadata only. It cannot turn failed
configuration, compilation or tests into success, and diagnostic errors do not
become a new native acceptance requirement. Cache keys/providers, compiler
tracking and build settings remain unchanged. Compare the fingerprints across
runs to narrow an ABI difference; equal reported fields do not establish equal
unreported inputs. This report is neither a cache-hit verdict nor a performance
measurement. In the investigated Windows pair, all 74 package ABIs differed;
the particular input causing that difference remains unproven.


## CI compiler object cache

Linux and macOS native jobs optionally use `Mozilla-Actions/sccache-action@v0.0.11`
with sccache `v0.16.0`. Preparation verifies the exact executable version before
exporting both C and C++ CMake compiler launchers. The compiler remains selected
by CMake; all platforms, dependency profiles, sanitizer flags, build targets,
parallelism, regressions and timeouts retain their existing settings. Windows
Debug jobs compile directly: their existing `/Zi` shared PDB configuration is not
changed to force compiler-cache hits.

The compiler archive has its own namespace, separate from the vcpkg archive.
Its compatibility prefix uses the complete existing dependency key, including
its exact image version, manifest/overlay fingerprint, compiler environment and
module/sanitizer/dependency profile. The source commit is only an immutable
snapshot suffix. There is no run ID or source commit in the compatibility prefix
or `SCCACHE_C_CUSTOM_CACHE_BUSTER`, so another commit with the same identity can
restore objects. There is no cross-image or cross-profile compiler restore.
sccache still validates compiler identity, compile arguments and source/header
inputs for every invocation; an archive restore alone does not establish an
object hit or bypass compilation validation.

Each job sets `SCCACHE_CACHE_SIZE=512M` under the runner temporary directory.
The full matrix has nine eligible Linux/macOS jobs, giving nine such local cache
budgets per source snapshot; Windows adds no compiler cache. Actions stores
immutable snapshots, which may overlap across commits and refs, so this is not a
repository-wide storage cap. Compression, metadata, dependency archives and cache
service retention are separate. Actions branch/ref visibility still applies;
queue refs cannot read each other's entries merely because their keys match.

Installation, version validation, cache restoration or server startup failures
leave an empty launcher and normal compilation. Cache service I/O failures use
`SCCACHE_IGNORE_SERVER_IO_ERROR=1`. Compiler and test failures still fail the job;
the workflow never retries a compiler failure without the launcher. Only successful
jobs save new compiler snapshots. A best-effort job summary reports dependency
archive availability, compiler archive exact hits and sccache statistics. Prefix
restoration and object hits are different observations.

Run `node --test tests/ci/*.test.mjs` for the cache identity/fallback and workflow
contracts. The permanent CMake/Ninja fixture compiles C and C++ through a launcher
path containing spaces and checks that intentional compiler errors propagate
with and without a launcher. It uses a transparent fixture launcher, not sccache,
and does not prove hosted cache hits. Hosted acceptance must compare a cold job
and a subsequent job with the same identity, record the source revisions,
archive lookup, sccache hits/misses and setup/configure/build/test durations,
and retain all required regressions. No build-time improvement is claimed before
that evidence is available. Native CI also needs separate Linux/macOS acceptance;
Windows remains uncached.
