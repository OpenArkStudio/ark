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

| Change | Source PR | Merge queue / manual |
| --- | --- | --- |
| Recognized documentation only | Nimbus and relevant README links | Same documentation checks |
| Reviewed application inputs | Related check targets and their CMake dependencies | Complete platform matrix and application build |
| Public interfaces, build, dependencies, CI or unknown inputs | Full affected application coverage; uncertain scope keeps all modules | Complete platform matrix |

All PRs require `CI - required`. Missing or failed classification falls back to full work. Target selection never reduces queue/manual coverage, archived integration construction checks, sanitizer coverage or test timeouts. Draft previews do not establish acceptance.

## CI build resources

Compile jobs are selected from detected CPU and memory, capped at four. A 7–8 GiB runner keeps two compile jobs and one link; a 16 GiB runner uses four compile jobs and one link. Two concurrent links require at least 19 GiB. These are scheduling estimates, not a guarantee about a translation unit's peak memory.

Ninja uses separate compile/link pools. `ARK_BUILD_JOBS_OVERRIDE` and `ARK_LINK_JOBS_OVERRIDE` may only lower the computed limits. Missing resource data falls back conservatively.

## CI dependency archive cache

vcpkg package archives and compiler objects use separate caches. Dependency keys retain OS/architecture, dependency manifests and overlays, features, profiles and compiler environment. Image-version fallback still requires each package's actual vcpkg ABI hash; compiler tracking is never disabled.

The existing **Dependency cache seed** workflow prepares Windows application dependencies on trusted `main`. It configures dependencies without compiling ark or repeating native tests. Seed availability is verified with an exact cache lookup. Required CI treats cache failures as optional and builds normally.

### Windows ABI diagnostics

Diagnostics report available vcpkg ABI metadata after configuration, including failed configurations. They do not infer missing hashes, change dependency selection or convert a failed build into a pass.

## CI compiler object cache

| Setting | Behavior |
| --- | --- |
| Linux/macOS | Pinned sccache 0.16.0; CMake retains the actual compiler |
| Windows Debug | Direct compilation; existing `/Zi` settings remain |
| Compatibility | Dependency/profile family retained; sccache validates actual compiler, flags, sources and headers |
| Snapshots | One immutable snapshot per family, trust scope and ISO week |
| Application snapshots | Written only by trusted `main`; PRs and queue restore them |
| Other source-PR profiles | May keep a bounded weekly snapshot within their own PR ref |
| Queue snapshots | Restore only; no duplicate uploads per candidate |

The **Compiler cache seed** workflow builds accepted Linux/macOS application inputs on `main`, without repeating the regression matrix. It also publishes matching dependency archives so later Unix configurations can reuse them. Superseded push inputs may skip this cache-only work; active acceptance runs are preserved. Linux application cache capacity is 1 GiB, macOS 512 MiB. Capacity is configurable from 64 to 1024 MiB; it is a local job budget, not a repository storage limit. This change does not increase paid cache storage or remove existing remote caches.

An exact weekly hit may reuse an earlier accepted snapshot without rebuilding it. Ordinary CI still compiles and validates its actual revision; new inputs can miss until the next snapshot. An archive hit is distinct from an object hit. [GitHub's cache access rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching) still apply.

## Verify CI changes

    node --test tests/ci/*.test.mjs

The tests cover scope fallbacks, workflow boundaries, compiler errors and real CMake/Ninja dependency and resource controls. Native and hosted acceptance remain separate.

To measure reuse, dispatch **Compiler cache seed** on `main` with `verify_reuse=true`. It explicitly clears only the job's local compiler objects, records a controlled cold build, cleans project outputs and records a warm rebuild with the same source and settings. Remote caches stay intact. Summaries identify source, archive lookup, build execution, sample type and object statistics. Compare setup, compilation and regression durations before claiming a speed improvement.
