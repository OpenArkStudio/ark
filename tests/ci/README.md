# CI policy tests

Run the native Node tests without installing npm packages (CMake 3.21 or newer
is required for the portable Windows runtime export fixtures):

```sh
node --test tests/ci/*.test.mjs
```

The tests use temporary local Git repositories. No GitHub API token, network service,
C++ rebuild or database is required. They verify complete rename-aware path decisions,
event-specific module selection, documentation availability and strict job results.

Source PRs run the related native modules on Linux with Boost, reusing the existing
configure/build/regression steps. Core/application tests select Applications;
network tests select Network; database tests select Database. Network/HTTP/Bus
plugin changes also select Applications, and MySQL/Redis/shared database support
changes select Applications and Database. Public core headers, shared build or
dependency inputs, CI changes and unknown paths select all three source modules.
Missing, invalid or empty enumeration falls back to the full 12-variant matrix.
Native changes run
the fatal clang-format 18 check. Documentation and README link checks retain their
existing path rules; source CI success reports module validation, not full matrix
acceptance.

Merge-group candidates compare the event's `base_sha` directly with `head_sha`,
including changes from earlier queued PRs. A candidate containing code, build,
dependency, CI or unknown paths runs all 12 native variants: Network on Linux,
macOS and Windows plus Linux AddressSanitizer（内存错误检测） with
UndefinedBehaviorSanitizer（未定义行为检测） and ThreadSanitizer（线程数据竞争检测）;
Applications on Linux, macOS and Windows; Database on Linux Boost, Linux
Standalone, macOS Boost and Windows Boost.
Manual runs retain the same full matrix. Only a known, nonempty documentation-only
range with an available site can skip native jobs. Queue candidates retain site
validation when the site is present, and README changes require their link check.

Documentation, root README links and C++ formatting share one `validation` job,
one checkout and one Node setup. Nimbus uses the npm package cache when its site
and lockfile are available; a required unavailable site still fails its preflight.
clang-format 18 is installed only when its command is absent. Each check keeps its
existing scope flag and exports its actual step `outcome`; later selected checks
still run after an earlier check fails. The scope job remains separate so native
matrices and lightweight validation can start together after classification.
This reduces the workflow from eight job definitions to six, or from 17 to 15
runner jobs when all 12 native variants and all lightweight checks are selected.
Actual waiting time depends on runner availability and check durations.

The required gate validates the actual workflow event and the declared validation
stage. Selected native jobs and the combined validation job must succeed;
unnecessary native jobs must be explicitly skipped. Each selected validation
step must report `success`, and each unnecessary step must report `skipped`.
A successful combined job cannot substitute for a missing or skipped required
step. Failed, cancelled, unknown or missing results fail; a source module result
cannot substitute for a queue result. Full acceptance still
requires the combined queue candidate. Protected `main` accepts only candidates
that passed their required checks; its resulting push does not repeat CI for the
same accepted tree. The separate documentation workflow still publishes on main
pushes. README badges display the merge-group workflow status.

Draft PRs retain their real scope and required-check flags. Scope classification,
documentation, root README links and formatting run when selected, but genuine
boolean `draft: true` metadata prevents new native jobs from starting. Only those
draft events rename the aggregate to `CI - draft preview` and select its separate
preview validator. A successful preview requires scope success, every selected
light step's actual `success`, every unselected light step's `skipped`, and every
native job's `skipped`. Its summary states that native acceptance was not executed
in this run and the PR is not admitted. It does not emit a successful
`CI - required` check. The final gate still rejects deferred admission.

An initial draft run therefore creates no `CI - required` context; the protection
rule keeps that exact required name. The intended Expected/pending display needs
hosted confirmation before deployment. Missing or non-boolean draft metadata
selects conservative full scope, retains the required name and final gate, and is
rejected. Light-check failures still fail the preview.

`ready_for_review` triggers fresh source validation with the existing module
selection; opening, updating and reopening ready PRs retain their normal checks.
`converted_to_draft` applies the hold to a new run. Neither event cancels an active
run. Code merge-group candidates and manual runs still require the full native
matrix, and CI policy changes still require every source module before the full
queue candidate is accepted. The workflow does not enqueue PRs. The root agent
alone admits the oldest unfinished PR by creation time; newer work stays local or
in draft with auto-merge disabled until earlier PRs are accepted. Scheduling-only
cancellations require fresh acceptance before admission. Returning an already
validated SHA to draft does not erase old check contexts. Keep auto-merge disabled
and require the fresh `ready_for_review` run to succeed before enabling auto-merge
or queueing; an earlier success on the same SHA is insufficient. Dynamic names do
not themselves enforce this freshness or a global admission order. Full code
queue validation remains necessary.

Each existing PR/ref concurrency group permits one active run and only the most
recently queued pending run. A new run waits for the active run to finish instead
of automatically cancelling it. Individual running jobs retain their configured
timeouts; those limits do not include pending time. Merge-group candidates use
their own refs. Independent groups do not enforce PR creation order. The required
gate still rejects failed, cancelled or missing results. Manual cancellation can
still prevent the `always()` summary from completing. See [GitHub concurrency rules](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#concurrency).

The exact root `AGENTS.md` path also qualifies as documentation. A known,
nonempty range confined to that file and other recognized documentation selects
Nimbus and the final required gate on both PRs and merge-group candidates, with
native jobs explicitly skipped when the site is available. This does not extend
the root README link check or exempt nested `AGENTS.md` files, similarly named
code, mixed source changes or uncertain ranges. Manual runs retain the full
native matrix. This CI policy change itself still requires native acceptance.

Within `src/`, only the exact plugin index paths `src/plugins/README.md` and
`src/plugins/README_CN.md` additionally qualify as documentation. Changes confined
to these indexes select the Nimbus check on PRs and documentation-only queue
candidates when the site is available. This does not extend the root README link
checker to plugin indexes: Nimbus checks the generated site, not the index files
themselves. Other Markdown under `src/`, nearby build/scripts, mixed code and
uncertain ranges retain the existing conservative native policy.

The four exact development guides `tests/core/README.md`, `tests/core/README_CN.md`,
`tests/database/README.md` and `tests/database/README_CN.md` also qualify as documentation.
They describe building and running tests; they are not runtime fixtures. A guide-only
change still selects Nimbus when the site is available, including without a `docs/`
change. This does not extend the root README link checker to test guides or allow
other test Markdown/README paths. Fixtures, source, build/dependency/CI inputs,
unknown neighbors, mixed changes and unavailable ranges retain their native checks.
This CI policy change itself requires all native modules before acceptance.

The two existing manuals `tests/database/cancellation.md` and
`tests/database/cancellation_CN.md` also describe opt-in commands; they are not
executed or read as build/runtime inputs. Their exact paths select documentation,
while mixed database C++ changes still select Database source validation and Nimbus.
Unknown test Markdown, scripts and build/dependency inputs remain conservative.
Git renames include both removed and added paths; deleting code retains native
validation. Code-bearing queue candidates still require every native variant.
This classification fix itself requires all source modules and full native queue
acceptance; it does not change already-running checks or exempt executable tools.

Root README link checks run only when a root README changes. They validate local file
targets, including images, HTML `srcset` and reference links. It handles the forms
used by these READMEs; it is not a complete Markdown parser. Remote URLs and heading
fragments are not validated. The Nimbus check separately validates links and anchors
within the generated documentation site, not root README fragments.

Windows Applications CI disables vcpkg's per-target app-local dependency copying
with `VCPKG_APPLOCAL_DEPS=OFF`. The pinned toolchain otherwise adds post-build
copy commands to shared libraries and executables, many of which use the same
output directory. After configuration, `windows-runtime.cmake` reads the actual
installed root, target triplet and build configuration from `CMakeCache.txt`.
It requires the matching dependency `bin` directory and exports only that directory
through `GITHUB_PATH`, so subsequent parallel builds, regression targets and their
application subprocesses inherit it. Debug uses `debug/bin`; release-family
configurations use `bin`, without a cross-configuration fallback. Invalid cache
inputs, a missing runtime directory or an unusable output channel fail the job.
Network, Database and ordinary developer builds retain their existing behavior.
The tests exercise real CMake cache loading, directory selection, subprocess PATH
inheritance and failure propagation. This removes shared app-local copy writes;
it does not identify the owner of an observed Windows file lock. Windows CI must
still validate actual DLL loading and all application regressions.

The vcpkg cache tests cover Windows/Unix archive paths, dependency and runner-image
key changes, provider exports, unavailable storage and explicit cold-build fallbacks.
The C++ CI jobs restore only local package archives through Actions cache; vcpkg
still validates each package ABI. A cache miss/service failure runs the ordinary
dependency build and tests. Archives are saved after successful CMake configuration,
before compiling and testing ark; later build or test failures still fail the job.
Workflow checks cover this order and retain fatal configure/build/test steps.
Missing runner-image identity or unsupported archive paths disable persistence and
export `VCPKG_BINARY_SOURCES=clear`, which prevents run-vcpkg's removed `x-gha`
default from being selected. Missing GitHub file-command channels fail explicitly.
No project object files, installed trees, credentials or compiler caches are persisted.


The v2 archive tests keep exact-image keys distinct across image rolls while
sharing one compatibility prefix. OS/architecture, `ImageOS`, vcpkg revision,
baseline, dependency/overlay bytes, feature/default selection, profile, compiler
flags and triplets isolate both lookups. Workflow tests require exactly one
restore and one exact-key save per native job, pass the one prefix only to restore,
and retain vcpkg compiler tracking and fatal configure/build/regression steps.
See [CI dependency archive cache](../../docs/project/contributing.md#ci-dependency-archive-cache)
for the ABI boundary, v1 cold transition and branch/ref visibility limits.
These tests do not execute vcpkg ABI matching or validate hosted cache hits.

The seed workflow tests restrict Windows Applications seeding to dependency/cache
input pushes on main or an explicit main dispatch. They require identical cache
identity, tools and complete Windows configure flags to required CI,
without ark compilation, regression execution or compiler caching. They do not
prove a hosted main-cache save or a later queue candidate's cache reuse; see
[CI dependency archive cache](../../docs/project/contributing.md#ci-dependency-archive-cache).
Seed-only checks reject disabled providers, incomplete keys and empty archives;
saving and exact-key availability are mandatory after a miss, while an existing
exact hit skips saving. A save action that only warns cannot bypass the final hit
check. Required CI's archive operations remain optional.


Windows ABI diagnostic tests use synthetic records in the format checked against
local installed Asio, rapidxml and Protobuf metadata. They cover exact-byte package
ABI hashing, fixed field-value fingerprints, CRLF, malformed/duplicate records,
missing metadata, the fixed installed paths, symbolic links and the 64 KiB limit.
CLI tests ensure raw values, unknown names, absolute paths and inherited secrets
never appear in output. Workflow tests retain Windows Applications-only placement
and fatal configuration/build/test behavior. No vcpkg build or hosted cache
acceptance is exercised by these tests. See
[Windows ABI diagnostics](../../docs/project/contributing.md#windows-abi-diagnostics).
