# Releasing

How to cut a CCCC release: version stamping, the tag workflow, and the
GitHub Actions release automation. See [BUILDING.md](BUILDING.md) for the
`release` build target and [TESTING.md](TESTING.md) for the audit the
release process gates on.

## Version stamping

`CCCC_RELEASE_VERSION` (`src/internal.h`) is the product version, distinct
from `CCCC_VERSION` (same file) — that one is the `.c4` bytecode container
format version, bumped independently and unrelated to releases.

`--version` prints `CCCC_RELEASE_VERSION`, plus a git describe string
(`CCCC_GIT_DESC`, stamped in by `build.c`/`Makefile` from `git describe
--tags --always --dirty` whenever a `.git` directory is present — absent
from release tarballs, which have no `.git`), the host triple, the bytecode
format version, and which optional features (`backtrace`, `decimal`,
`curl`) this binary was built with.

```
$ ./cccc --version
cccc 0.1.0 (v0.1.0-3-gabc1234)
host: aarch64-darwin
bytecode format: v1
features: backtrace
```

Bump `CCCC_RELEASE_VERSION` in `src/internal.h` and commit it before
tagging — `tools/release.sh` refuses to tag if the source constant doesn't
match the version you're releasing.

## Release build mode

```
./cccc --build build.c --build-target=release
```

Builds `cccc-release` at `-O2 -g -DNDEBUG` (the *host* compiler optimizing
the cccc binary itself — unrelated to CCCC's own guest-side `--optimize`
bytecode passes or `-0`/`-1`/`-2`/`-3` safety levels, neither of which this
changes). `-g` is kept even in release builds; symbols are stripped at
packaging time by the release workflow, not at compile time, so a release
binary a user hands back a crash report from is still debuggable locally.

## Audit gate

Before tagging, run the full matrix that ticket #883 established:

```
./cccc --build build.c --build-target=test         # baseline
./cccc --build build.c --build-target=opt_test_O1   # host -O1
./cccc --build build.c --build-target=opt_test_O2   # host -O2
./cccc --build build.c --build-target=opt_test_O3   # host -O3
./cccc --build build.c --build-target=cccc_ubsan    # then run the suite against cccc-ubsan
./cccc --build build.c --build-target=cccc_asan
./cccc --build build.c --build-target=cccc_tsan
```

The `opt_test_*` targets exist to catch UB the interpreter gets away with at
`-O0` (aliasing, uninitialized reads, signed overflow) that only misbehaves
once the host compiler optimizes across it. A systematic failure here
(many tests, or any miscompile) is grounds to pause the release and fix the
root cause before proceeding; an isolated failure gets its own ticket and
ships in the next patch release instead.

`linux-x86_64-msan-test`'s known uninstrumented-libc/libffi blind spot
(documented in [TESTING.md](TESTING.md)) is expected noise, not a release
blocker.

## Tagging

```
sh tools/release.sh 0.1.0
```

Verifies: clean working tree, `CCCC_RELEASE_VERSION` matches the requested
version, `CHANGELOG.md` has a matching `## [0.1.0]` section, then runs the
full `test` build target. On success it creates an annotated `v0.1.0` tag
locally (message = the matching `CHANGELOG.md` section) and prints the push
commands — it does **not** push anything itself. Review the tag, then push
deliberately:

```
git push origin trunk
git push origin v0.1.0
git push github trunk
git push github v0.1.0
```

`trunk` is pushed to both remotes on every ordinary push too, not just at
release time (see [CLAUDE.md](../CLAUDE.md)'s Branching notes) — `github`
needs to stay current since it hosts the Doxygen docs (GitHub Pages, via
`.github/workflows/ci.yml`, deployed on every push to `trunk`). What's
release-specific here is the **tag** push: pushing a `v*` tag to `github`
is what triggers `.github/workflows/release.yml` (see below), which is
also where the three non-amd64 quadrants (Linux aarch64, macOS arm64,
macOS x86_64) get built and tested — `origin` (sr.ht) always runs
`.builds/linux-amd64.yml` on every push regardless of tags, but GitHub's
regular-push workflow is docs-only, so those three quadrants are only
exercised at release time.

## GitHub release automation

`.github/workflows/release.yml` triggers on `v*` tag pushes. Four jobs
(linux-amd64, linux-aarch64, macos-arm64, macos-x86_64) each bootstrap,
build the `release` target, run the full test suite against that exact
release binary, then package `cccc-<version>-<os>-<arch>.tar.gz` (binary +
`LICENSE` + `README.md`), emit `SHA256SUMS`, and publish everything to the
GitHub release for that tag. This is the only place Linux aarch64/macOS
arm64/macOS x86_64 get built and tested at all — GitHub's regular-push
workflow (`ci.yml`) only builds and publishes the Doxygen docs, so those
three quadrants have no per-push coverage between releases.

Testing the release binary itself (not just the debug build) before
publishing is deliberate — an artifact that skipped the audit gate on its
own platform defeats the point of gating on it in the first place.
