# Contributor and agent instructions

These rules apply to this repository. Preserve unrelated work and use an isolated
branch or worktree when the existing checkout contains uncommitted changes.

## Commits and pull requests

- Write all new commit subjects and PR titles in English using Conventional
  Commits: `type(scope): description`. Use a short, lowercase scope and an
  imperative description of the resulting change.
- Types: `feat`, `fix`, `perf`, `refactor`, `docs`, `test`, `build`, `ci`, `style`,
  `chore` and `revert`. Choose the type that describes the actual change; use
  `style` only for formatting changes without behavior changes.
- Examples: `feat(config): support TOML application manifests`,
  `fix(logging): preserve the configured log directory`,
  `ci(cache): restore vcpkg package archives`.
- Name new topic branches `type/short-kebab-case`, matching the change, for example
  `feat/database-coroutines`, `fix/windows-includes` or `docs/readme-maintainers`.
  Do not use the `codex/` prefix or generic `xxx-branch` names. Existing published
  branches do not need renaming solely to adopt this rule. Use lowercase names
  for new GitHub repositories.
- Mark incompatible public changes with `!` and explain them in a
  `BREAKING CHANGE:` footer. Describe migration steps in the PR and release notes.
- Keep each PR focused on one functional module or related change. Publish
  independently reviewable work instead of accumulating unrelated modifications.
- All changes to `main` go through a PR. Do not push directly, force-push or delete
  `main`, bypass branch protection, or rewrite already merged history.
- Use squash only after every required CI check passes and review conversations
  are resolved. Outside the merge queue, synchronize the PR head with `main`
  before acceptance. With the merge queue enabled, validate the queue candidate
  containing the latest `main` and preceding queued changes; a green PR head alone
  does not validate that combination. Admit eligible PRs in the creation order
  defined below.
  Another maintainer's approval is not currently mandatory. Merge eligible PRs
  without asking for confirmation again.
- Explicitly set the squash commit subject to the Conventional Commit PR title.
  Replace GitHub's generated commit-list body with a concise change description;
  do not copy intermediate commit subjects into release notes.

## Verification and delivery

- Follow the root scheduler and per-repository PR order below when runner
  capacity is shared. Prepare newer work locally and defer hosted admission;
  do not repeatedly push newer branches ahead of the backlog. Preserve active
  acceptance runs. A cancelled scheduling run still needs fresh successful checks
  before acceptance.
- Run the checks relevant to the change before publishing. Report the exact
  tested revision, commands, results and material limitations.
- Investigate failed CI logs before changing code or retrying. Do not repeatedly
  rerun an unexplained failure, weaken tests or protection, or extend timeouts to
  hide a defect.
- Pending, skipped, cancelled and successful checks are distinct states. A
  cancelled run is not validation. If a run is cancelled for scheduling, record
  the reason and resume its acceptance checks; do not leave it indefinitely red.
- Use the repository's change-scope policy: documentation-only changes run the
  relevant documentation/link checks. Source PRs run the affected native modules
  on Linux Boost and clang-format 18; shared public interfaces, build, dependency,
  CI and unknown paths require every source module when the changed range is known. Unavailable,
  malformed or empty ranges fall back to the full matrix. Code merge-queue
  candidates and manual runs retain the full cross-platform and sanitizer matrix.
  Source checks admit a PR to the queue;
  they do not establish final matrix acceptance. Never fabricate a passing result.
- Include what changed, why, validation and remaining limits in PR descriptions.
  Verify the merge result before saying that a PR has merged.

## C++ and repository style

- Use C++20 and consistent `snake_case` names, including types, functions and
  members (`ark::plugin_manager`, `find_module()`, `module_name_`). Do not manually
  edit generated protobuf output; change the schema or generation source.
- Use lowercase `snake_case` for plugin CMake targets and shared-library stems,
  including official, server and sample plugins (`ark_mysql_plugin`,
  `timers_plugin`). Keep application manifests, launch scripts and documentation
  aligned with the generated names; retain platform extensions and Debug `_d`.
  Name bundled examples by capability and keep their directories, C++ types,
  targets and application manifests aligned; do not introduce numbered sample names.
- Keep production code in `src/`: core headers and implementation in `src/core/`,
  configuration in `src/config/`, plugin sources in `src/plugins/<name>/` and the
  launcher in `src/application/`. Co-locate hpp/cpp by function. Do not recreate
  a top-level application source directory or split source headers into include/. Retain the current `app` CMake target and the
  `app`/`app_d` executable names.
- Follow `.clang-format` and the repository formatting scripts with clang-format
  18. Keep function opening braces on the declaration line, expand short function
  bodies, and use braces for control statements.
- Group includes as system, third-party, then project headers. Keep required
  platform-header dependencies in a valid order. Separate include blocks,
  namespaces and adjacent definitions with the prescribed blank lines.
- Run `CLANG_FORMAT=/path/to/clang-format bash scripts/format.sh --check`.
- The formatter selects Git-tracked files. Stage intended new C++ files before
  running it, or format those paths explicitly. Run the complete check again
  after committing so newly tracked tests and headers cannot escape validation.
- Keep optional compiler caching separate from vcpkg archives. Preserve compiler
  flags, matrix coverage and failure propagation; use an empty launcher when cache
  setup is unavailable. Windows Debug remains uncached without changing `/Zi`.
  Record hosted cold/warm cache evidence before claiming a speed improvement.
- CI policy tests: `node --test tests/ci/*.test.mjs`. Nimbus documentation:
  `npm ci --prefix docs/site` followed by `npm run check --prefix docs/site`.
- Keep official plugins in the ark repository and application business plugins
  in application repositories. The approved next plugin layout co-locates public
  headers, private declarations and implementation files inside each plugin;
  do not reintroduce a mandatory per-plugin include/src split. Keep public header
  mappings explicit and independent from physical paths. Public includes retain the
  `<ark/...>` project prefix; moving source must not expose `<core/...>` or
  `<plugins/...>` paths to consumers. Concrete implementation
  types may live in cpp files when registration and tests no longer need their
  definitions. Public-header mapping and creation-macro replacements
  require the agreed design before implementation; do not silently change ABI,
  lifecycle or ownership. Retain independent plugin targets and optional builds.
  Public plugin/module hpp and cpp sources belong together by function. Generated
  build-tree header mappings preserve logical `<ark/...>` paths; they are not a
  second source tree. Keep the existing CMake and `check-sdk` machine identifiers.
  Do not turn interface-header mapping into a separate public SDK product.
  Public plugin installation and packaging use GNUInstallDirs, explicit header
  inventories and relocatable CMake exports; preserve the source aliases and
  logical <ark/...> paths. Install arknet separately through its own package.
  Never export source/build paths, bundle arbitrary dependencies or claim
  dependency-free runtime redistribution. Keep installed-consumer relocation
  tests and bilingual installation/build/layout documentation synchronized.
  Keep application manifest declarations and the compiled file/TOML reader in
  `src/config/`. Preserve `<ark/application_manifest.hpp>` through the always
  available `ark::config_headers` mapping, including with `ARK_BUILD_CONFIG=OFF`.
  Applications and Bus/HTTP/Consul share this reader; schema validation belongs
  to the application or respective plugin. Keep toml++ private and MySQL/Redis
  connection configuration programmatic. Configuration-only targets must not
  acquire core, arknet or runtime dependencies; compiled configuration must be
  position-independent for shared-library consumers.
  Kernel retains all existing `ark/kernel/...` headers, including public
  implementation/value templates and `kernel_plugin.hpp`. Its typed construction
  uses module_base for the original interfaces, plugin_base, private factories
  and explicit entry points. Do not hide/remove retained public types. Preserve
  interface RTTI keys, explicit update selection and registration order; public
  class layout changes require a breaking notice and rebuilding consumers.
  Keep private factory headers outside public mappings and installation.
  All official plugin families now use explicit public-header mappings from
  `src/plugins/<name>/`; do not restore header discovery under `include/ark/`.
  Consul, Etcd Client, Kafka, OSS SDK and Physic retain every existing public
  header, including implementation and assembly headers, byte-for-byte during
  physical moves. Their layout does not validate optional driver integrations;
  retain the explicit unsupported PhysX 3 build failure.
  Verify accepted source before claiming the new layout is available. Hot-update
  and Registry design discussions remain paused.

## Documentation and releases

- Keep English and Chinese documentation aligned. Keep the README concise and
  place detailed usage, architecture and validation material under `docs/`.
- Write release notes for users: summarize core capabilities and group relevant
  changes under Features, Improvements, Fixes and Upgrade notes. Omit empty
  sections and combine implementation details into their user-visible effect.
- Do not publish a commit-by-commit diary, file list, formatting list or test
  assertion inventory. Link detailed validation and performance reports instead.
  Retain compatibility changes, security requirements and known limitations.
- Only describe changes contained in the release tag. Verify the release commit,
  assets and checks before publishing; never move an existing release tag to
  make unreleased work appear included.
- Keep raw measurements and build logs outside the source repository. Published
  performance claims must identify the machine, workload, units and tested
  revision; local tests do not establish production PCU capacity.
- Guest-room client/server message bodies use the canonical
  `src/proto/proto/guest_room.proto`; Entity definitions and profiles remain JSON.
  Use lowercase `snake_case` for every project `.proto` filename, message, enum,
  enum value, field and handwritten oneof. Keep field numbers, types and enum
  numbers stable during naming changes; retain existing JSON keys explicitly
  with `json_name` where required. Document generated-source API and text/JSON
  enum migrations separately from binary compatibility. Run `check-proto-naming`;
  compiler-generated map entries and optional-field oneofs are not source names.
  Synchronize the Godot schema, generated C# and golden vectors with wire changes,
  verify cross-repository parity and run the real two-client integration before
  claiming acceptance. Preserve owning scalar capture, explicit authorized
  fields, owner-thread flush, revision checks and bounded pending messages.
  Incompatible wire changes require coordinated server/client upgrades and a
  migration notice; never hand-edit generated Protobuf output.

## Task execution and CI admission

- Use `gh` CLI for GitHub operations; do not replace it with direct REST/GraphQL
  requests or `gh api`. Normal Git fetch/push commands remain appropriate.
- The root agent alone observes CI and controls PR readiness and queue admission.
  Order queue admission by PR creation time within each repository, retaining
  explicit user-approved priority exceptions. A newer PR becoming green first
  does not permit it to overtake an older unfinished PR in the same repository.
  Independent repositories may advance in parallel.
- Before admitting new source/native work or requesting a new merge candidate,
  verify that every older active candidate has started all of its native jobs.
  A waiting or missing native job is not a start; completion is not required.
  Unadmitted work in another repository does not block an eligible repository.
- Continue independent implementation and local validation in parallel. Use drafts
  only when the deployed workflow actually defers draft native jobs; otherwise
  retain a reviewable local branch until admission is available.
- Do not enable automatic merge for held PRs. Release them in creation order
  within each repository, retaining explicit user-approved priority exceptions;
  after required checks pass, squash through the protected merge queue promptly.
  Do not repeatedly ask for already-authorized merges or bypass any check.
- When a draft-preview workflow is deployed, require its fresh `ready_for_review`
  run to succeed at the intended head before enabling auto-merge or queue admission.
  Preview success, an absent required context, or older green checks on the same
  SHA do not establish fresh admission. Verify the hosted behavior before claiming it works.
- Give jobs unique, descriptive names. Consolidate compatible jobs/steps to reduce
  setup time without dropping required platforms, protocols or verification.
- Preserve active acceptance runs. Do not cancel older runs to admit newer work.
  Cancellation never counts as a successful test. GitHub Actions concurrency
  groups do not by themselves enforce PR-creation order.
- Keep one observer per PR batch. Batch status reads, back off unchanged polling,
  and let scripts collect logs and exit codes. Read summaries and relevant failure
  excerpts, not entire repeated logs. Communicate completion, failures, interface
  changes and decisions; do not repeatedly ask agents for unchanged progress.
- Delegate only concrete independent deliveries with a scope and acceptance
  criteria. Do not wake finished agents just to keep them busy. Record a short
  handoff at module completion and preserve unrelated work in dirty checkouts.
- Current-session monitoring does not authorize recurring/background automation.
  Stop owned observers before ending the session unless separately authorized.

## Keep documentation synchronized

- Choose Nimbus components by content type to reduce repetition: routing cards
  for overviews, steps for procedures and tabs for supported alternatives. Keep
  reference constraints visible; preserve bilingual labels, links and code in
  both rendered pages and complete AI Markdown exports.
- Resolve legacy page aliases to their canonical source before editing. Nimbus
  may skip retired Markdown files even when they remain in Git. Check the emitted
  HTML and Markdown for the changed content; a successful build alone does not
  prove that an edited source is published.
- Treat documentation as part of each functional change. Review both languages,
  README, feature/support tables, API and client/server examples, configuration,
  installation, lifecycle, navigation and affected performance/validation reports.
  Update the relevant material in the same PR; do not routinely defer it.
- Describe the final supported behavior. Remove stale TODO/experimental/private
  development-layer claims when accepted code supersedes them, and avoid
  redundant "implemented", "retest" or "latest test" labels. Keep actual future
  capabilities clearly marked TODO; do not imply they already work.
- Keep ark and arknet boundaries clear. Cross-link the other project's accepted
  public APIs rather than copying them or claiming pending integration is merged.
  Never attribute later main changes or unpublished results to an older release.
- Run the relevant Nimbus/link checks. After publication, verify the Pages run
  succeeded for the intended source revision and inspect affected published pages.
  A local preview, merged PR and deployed site are distinct evidence states.
- Use the full names AddressSanitizer, UndefinedBehaviorSanitizer and
  ThreadSanitizer in explanatory prose and human-readable job/step display names. Preserve command flags, paths, workflow
  identifiers and machine-readable fields. Call Asio/Beast dependencies rather
  than backends; capitalize Standalone Asio in prose.
- Prefer SVG or Mermaid for editable diagrams and charts. Show labeled units and
  readable legends, retain useful error bars, and avoid repeating chart contents
  in long prose. Keep the README concise, with properly formatted dependency lists;
  detailed guidance belongs in the bilingual documentation site.

## Evidence and performance boundaries

- Keep raw measurement JSON, packet captures, logs and credentials local, outside
  version control. Do not upload them as release assets, CI artifacts or public
  downloads without separate explicit authorization for those exact files.
- Extend existing doctest correctness tests rather than adding a Boost test
  dependency. Prefer standard C++20 facilities over obsolete wrappers/macros when
  behavior and ownership can be preserved; keep necessary platform/ABI boundaries.
- Maintain compile-and-run C++ tests and benchmarks in their project directories;
  do not require Python or Ruby as the user's test entry point. Shell scripts may
  orchestrate internal batches. Do not replace permanent tests with temporary ones.
- Measure performance locally before writing reports. Identify machine, OS,
  compiler, dependency versions, source/binary identity, duration and units.
  Compare payload sizes, in-flight batches, repeated runs, coroutine/callback
  interfaces and IO/thread models, including one context/one thread and one
  context/multiple threads. Report throughput, latency and CPU utilization;
  100% CPU denotes one logical core, with the measurement scope stated.
- Retain the final valid results and material limitations, not a retesting diary.
  Functional checks, sanitizer checks, performance measurements and deployment
  capacity are separate claims. Loopback tests do not prove production PCU or
  cross-host capacity; successful controls do not establish an unresolved root
  cause. Do not extend timeouts or rerun to green to conceal failures.
