---
title: Continuous integration and git hooks
section: Tooling
order: 50
status: implemented
summary: The GitHub Actions workflow that builds, tests and checks the engine and its manual on every push and pull request, and the local pre-push hook that runs the same checks before code leaves your machine.
sources: .github/workflows/, .github/scripts/
---

## Overview

Detergine has one GitHub Actions workflow, [ci.yml](.github/workflows/ci.yml) (workflow name `CI`). It checks that
every change builds and passes its tests on the supported desktop hosts, that it is clean under sanitizers, that the
runtime still builds for Android, that simulation results are identical in Debug and Release, and that the engine
manual is valid and was updated together with the code it documents.

Every build and test job runs the same `dg` commands you run locally (`python tools/dg.py ...`), with the same
presets. The `docs` job calls the manual builder ([build_docs.py](tools/docs/build_docs.py)) directly, which is what
`dg docs` wraps. A CI failure therefore reproduces on your machine with the command shown in the failing step. See [The dg developer tool](dg-tool.md)
for the commands themselves.

The **pre-push hook**, installed with `./dg hooks install`, runs the fastest part of these checks locally before each
`git push`.

| Job (display name) | Runner | Proves |
|---|---|---|
| `docs` (Manual) | `ubuntu-24.04` | The manual builds, its links and source coverage are valid, and changed sources come with doc updates |
| `test` (macOS ARM64) | `macos-15` | Builds and tests pass in Debug, Development and Release; Debug and Release sim hashes match |
| `test` (Windows x64) | `windows-2025` | The same on Windows with Ninja and clang-cl |
| `test` (Linux x64) | `ubuntu-24.04` | The same on Linux with Clang 19 |
| `sanitizers` (Linux x64 ASan + UBSan) | `ubuntu-24.04` | No memory errors or undefined behaviour in the Debug test run |
| `android` (Android ARM64 (build only)) | `ubuntu-24.04` | The runtime builds for Android ARM64 in Release |

## How to use

### Triggers

The workflow runs on:

* every push to `main`,
* every pull request (any target branch),
* manual runs (`workflow_dispatch`): open the workflow in the repository's **Actions** tab and choose **Run
  workflow**, or run `gh workflow run CI`.

Runs are grouped by `ci-<git ref>` with `cancel-in-progress: true`. A new push to a branch or pull request cancels the
run that is still going for the same ref, so only the latest commit is checked. Every job has `timeout-minutes: 45`,
and every CTest test a 300-second timeout, so a hang fails quickly instead of holding a runner for hours.

### What each job runs

**`docs`** checks out the full history (`fetch-depth: 0`) and runs the manual builder directly, without CMake:

```bash
python tools/docs/build_docs.py --check --changed-since "$BASE"
python tools/docs/build_docs.py
```

`BASE` is the pull request's base commit for pull requests, and the commit before the push (`github.event.before`)
for pushes. If `BASE` is empty, all zeros (a newly created branch) or not present in the clone (after a force push),
the job runs `--check` without `--changed-since`. The built site is uploaded as the workflow artifact
`detergine-manual`, which you can download from the run's summary page. This is the same check as
`./dg docs --check --changed-since <ref>`; see [Writing documentation](../contributing/documentation.md).

**`test`** is a matrix over three hosts with `fail-fast: false`, so one failing host does not cancel the others. Each
entry runs:

```bash
python tools/dg.py setup --vcpkg --yes
python tools/dg.py test debug   --preset <preset> -D DG_WARNINGS_AS_ERRORS=ON
python tools/dg.py test dev     --preset <preset>
python tools/dg.py test release --preset <preset>
python tools/dg.py determinism  --preset <preset>
```

| Matrix entry | `os` | `preset` |
|---|---|---|
| macOS ARM64 | `macos-15` | `macos-arm64` |
| Windows x64 | `windows-2025` | `windows-x64` |
| Linux x64 | `ubuntu-24.04` | `linux-x64` |

`-D DG_WARNINGS_AS_ERRORS=ON` is stored in the build tree's CMake cache by the Debug step. The Development and Release
steps reuse the same multi-config build tree, so all three configurations build with warnings as errors. Each
`dg test` step builds everything and runs the whole CTest suite, including the golden-hash and smoke tests (see
[Testing](testing.md)). The last step, `dg determinism`, runs `dg-player --headless --fast --ticks 1800` in Debug and
Release and fails if the final `state-hash` values differ.

**`sanitizers`** runs `dg test debug --preset linux-x64 -D DG_ENABLE_ASAN=ON -D DG_ENABLE_UBSAN=ON
-D DG_WARNINGS_AS_ERRORS=ON`, the Debug suite under AddressSanitizer and UndefinedBehaviorSanitizer.

**`android`** sets `ANDROID_NDK_HOME` from the runner's `ANDROID_NDK_LATEST_HOME` and runs
`dg build release --preset android-arm64`. It only builds: the `android-arm64` preset sets `DG_BUILD_TESTS=OFF`, and
Android binaries cannot run on the Linux runner.

### Toolchain installs

All jobs install Python 3.12 with `actions/setup-python`. The rest depends on the host:

| Host | Installs |
|---|---|
| macOS | `brew install ninja pkgconf` (AppleClang comes with the image) |
| Linux (`test`, `sanitizers`) | [install-linux-tools.sh](.github/scripts/install-linux-tools.sh) |
| Linux (`android`) | `apt-get install ninja-build clang pkg-config` (the NDK brings its own compiler) |
| Windows | `pip install ninja`; adds `C:/Program Files/LLVM/bin` to `PATH` if `clang-cl` is not found. `dg` loads the Visual Studio developer environment through `vswhere`. |

[install-linux-tools.sh](.github/scripts/install-linux-tools.sh) installs Ninja and pkg-config (vcpkg ports need it)
and Clang 19. If the distribution has no `clang-19` package, it falls back to LLVM's official `llvm.sh` apt installer.
It then links `clang-19` and `clang++-19` to `/usr/local/bin/clang` and `/usr/local/bin/clang++`, because the
`linux-x64` preset asks for `clang` and `clang++`. Clang 19 or newer is required: older Clang cannot compile
libstdc++'s `std::expected`.

### Caching

vcpkg builds every dependency from source, which is the slowest part of a cold run. The workflow sets
`VCPKG_DEFAULT_BINARY_CACHE` to `.vcpkg-cache` in the workspace and saves that directory with `actions/cache`:

| Job | Cache key | Restore fallback |
|---|---|---|
| `test` | `vcpkg-<preset>-<hash of vcpkg.json and tools/dg.py>` | `vcpkg-<preset>-` |
| `sanitizers` | `vcpkg-linux-x64-<hash>` | `vcpkg-linux-x64-` |
| `android` | `vcpkg-android-arm64-<hash>` | `vcpkg-android-arm64-` |

`tools/dg.py` is part of the key because it pins the vcpkg release (`VCPKG_RELEASE`) that `dg setup --vcpkg` clones.
When the key misses, the newest cache with the same prefix is restored, so only changed packages are rebuilt. The
`sanitizers` job shares its cache with the Linux x64 `test` entry, because sanitizer flags apply to engine targets,
not to vcpkg packages.

### Read a failure

Open the failing job and expand the first red step. The step name tells you what failed and how to reproduce it.
Install steps are named differently per job: `Install tools (macOS)`, `Install tools (Linux)` and
`Install tools (Windows)` in the host test jobs, and unnamed steps that GitHub shows as
`Run ./.github/scripts/install-linux-tools.sh` (sanitizers) and `Run sudo apt-get update && ...` (Android).

| Failing step | Meaning | Reproduce locally |
|---|---|---|
| Check pages, links, coverage, and doc updates for changed sources | A page is malformed, a link is broken, a source is undocumented, or code changed without its page | `./dg docs --check --changed-since origin/main` |
| Install tools (...) / Run ... install | Runner image or package mirror problem | Usually re-run the job |
| Bootstrap vcpkg | vcpkg clone or bootstrap failed | `./dg setup --vcpkg` |
| Test Debug / Development / Release | Compile error, warning (treated as an error), or failing test | `./dg test <config> -D DG_WARNINGS_AS_ERRORS=ON` |
| Determinism (Debug vs Release) | `DESYNC`: Debug and Release produced different sim state | `./dg determinism` |
| Test Debug with sanitizers | ASan or UBSan report | `./dg test debug -D DG_ENABLE_ASAN=ON -D DG_ENABLE_UBSAN=ON` (Linux or macOS) |
| Build Release (Android job) | Runtime code does not build for Android | `ANDROID_NDK_HOME=<ndk> ./dg build release --preset android-arm64` |

For test failures, `dg test` passes `--output-on-failure` to CTest, so the log contains the full GoogleTest output of
each failing case under its `<executable>.<Suite>.<Test>` name. The CTest summary at the end of the step lists every
failed test. A golden-hash test prints both the measured and the expected value. If it fails only on Windows or
Linux, the code is not cross-platform deterministic; do not re-record the value (see [Testing](testing.md)).

A failure in the documentation check prints one line per problem. For a changed source without a page update, the
message names the source and the pages that document it. Update one of those pages, or, if the change has no visible
effect, add a `Docs: none (<reason>)` line to a commit message in the pushed range.

> **Warning:** The `Docs: none` trailer is not per-file. If any commit message in the checked range contains it, the
> check skips every changed source in that push or pull request. Use it only when none of the changes in the range
> need a page update.

### The pre-push hook

```bash
./dg hooks install     # write .git/hooks/pre-push
./dg hooks             # show whether it is installed (same as `./dg hooks status`)
./dg hooks uninstall   # remove it
```

The hook changes to the repository root and runs:

```bash
python3 tools/dg.py docs --check || exit 1
exec python3 tools/dg.py test dev -L unit
```

It validates the whole manual (without `--changed-since`), then builds Development and runs every test with the
`unit` label. The smoke tests are skipped to keep pushes fast. To skip the hook once, run `git push --no-verify`. CI
still runs every check.

`dg hooks install` refuses to overwrite a `pre-push` hook that it did not create; it recognizes its own hook by a
marker comment on the second line. Merge an existing hook by hand. The hook is not installed by default; run
`./dg hooks install` once per clone.

## How to edit

The workflow lives in [.github/workflows/ci.yml](.github/workflows/ci.yml), the Linux toolchain script in
[.github/scripts/install-linux-tools.sh](.github/scripts/install-linux-tools.sh), and the hook text in
`PRE_PUSH_HOOK` in [tools/dg.py](tools/dg.py).

Keep these invariants:

* **Jobs call `dg`, not raw CMake.** A new check goes into `dg` (or the docs builder) first and then into CI, so it
  can be reproduced locally with one command.
* **Every target platform stays covered.** CLAUDE.md requires every change to build on Windows x64 and macOS ARM64,
  and runtime modules on Android ARM64. Do not remove a matrix entry or the `android` job without an ADR.
* **Warnings stay errors.** Keep `-D DG_WARNINGS_AS_ERRORS=ON` on the first build step of each test job.
* **Determinism stays checked on every host.** Keep the `dg determinism` step in the matrix.
* **Cache keys include everything that changes package builds.** If another file starts to influence vcpkg (for
  example a new overlay triplet in `cmake/triplets/`), add it to the `hashFiles(...)` of every cache step.

Common changes:

* **Add a host or preset.** Add an entry to `strategy.matrix.include` with `name`, `os` and `preset`, plus an
  `Install tools` step if the host needs one.
* **Add a check to every host.** Add a step after `Determinism (Debug vs Release)` that calls `python tools/dg.py ...`.
* **Change the Linux compiler.** Edit `install-linux-tools.sh`. Keep the `clang`/`clang++` symlinks, because the
  `linux-x64` preset names those compilers.
* **Change the pre-push hook.** Edit `PRE_PUSH_HOOK` in `tools/dg.py`. Existing clones get the new hook only after
  running `./dg hooks install` again, which overwrites the old hook because it carries the marker line.

When you change a job, update the job and failure tables on this page in the same change. When a step starts calling a
different `dg` command, also check [The dg developer tool](dg-tool.md).

## Testing

You cannot run GitHub Actions locally, so test a workflow change in two ways:

1. Run each changed step's command on your machine (the commands are listed under *What each job runs*). For the
   Linux script, run it in a fresh `ubuntu-24.04` container or VM; it uses `sudo` and changes `/usr/local/bin`.
2. Push the change to a branch and open a pull request, or start the workflow by hand with
   `gh workflow run CI --ref <branch>`. Check that every job ran (none skipped by a wrong `if:`) and that the cache step
   reports a hit on the second run.

To test the pre-push hook, run `./dg hooks install`, then `./dg hooks` to confirm the status, and push to a branch.
The hook output appears in your terminal before the push starts.
