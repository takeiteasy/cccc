# Parallel and Incremental Builds

Job control, the build cache and header dependencies.

## Parallel builds

`--build-jobs=N` operates at two levels that share the same `-j` slot budget:

**Target-level (N>1, multiple ready targets):** when two or more targets have all
their dependencies satisfied at the same time, the runner forks up to N child
processes and builds them simultaneously. Each child compiles its sources serially
(`jobs=1`) so the total number of concurrent compiler invocations does not exceed N.
Output from concurrent targets interleaves (same behaviour as `make -j`).

**Source-level (N>1, lone ready target):** when only one target is unblocked, it
runs in-process with the full `-j` budget, allowing up to N `cc -c` processes to
compile that target's sources simultaneously.

**Non-POSIX / `--build-dry-run`:** always uses the serial path regardless of `-j`.

**Known limitation:** idle slots are not redistributed. If two targets build in
parallel under `-j4`, each gets one serial source slot — the two idle slots are not
reclaimed for additional source parallelism within the children.

The toolchain used to compile/link `--build` targets themselves (including
cccc's own bootstrap) is selected via `CCCC_BUILD_CC` (else `cc` / `clang` /
`gcc`); `ar` is selected via `CCCC_BUILD_AR` (else `ar`). This is a separate
env var from `CCCC_NATIVE_CC`, which only selects the compiler `-c=native`
hands its *generated* C to — setting `CCCC_NATIVE_CC` to test
`-c=native` under a different compiler no longer affects which compiler
builds `--build` targets.

A real gcc (e.g. Homebrew's `gcc-16` on macOS) is a supported `CCCC_BUILD_CC`
value: `CCCC_BUILD_CC=/opt/homebrew/bin/gcc-16 ./cccc --build build.c
--build-target=cccc` builds and links cleanly.


## Incremental builds and header dependencies

Under `--build-cache`, every native compile also gets `-MMD -MF
<objdir>/<stem>.d`, giving the incremental cache visibility into headers a
source `#include`s — not just the `.c`/`.cpp` file itself:

- **Level 1 (mtime).** An existing `.o` is only considered current if it is
  at least as new as its source **and** every header prerequisite listed in
  its `.d`. A source with no `.d` yet (a first build, or an objdir from
  before this tracking existed) is never trusted by mtime alone — it
  self-heals on its next build rather than silently serving a stale object.
- **Level 2 (content-hash CAS).** The cache key folds in the *content* of
  every header prerequisite, not just the `.c` file's — a header edit that
  doesn't happen to change the source's own mtime relationship still
  produces a different key. Crucially, a source **with no `.d` file present
  in its objdir never touches the CAS at all** (neither lookup nor store):
  `--build-cache=PATH` is a *global* content-addressable store shared across
  every objdir that points at it, while `.d` files are per-objdir, so a
  fresh objdir sharing a warm cache could otherwise "hit" an object some
  other objdir compiled against different header content. The very first
  compile of a source in a given objdir always runs for real and stores
  under the key computed from the `.d` it just wrote.
- **Link/archive-step staleness.** The link (`cc -o ...`) or archive (`ar
  rcs ...`) step is skipped — reported as `(up to date) <target>` — when the
  output already exists, is at least as new as every object file, and an
  argv hash stamped in the objdir (`<objdir>/.cccc-link`) matches the
  current link command line. Before this, no target had any incremental
  check at the link step; a native EXE/STATIC/DYNAMIC target relinked
  unconditionally every build even when nothing changed.

All of this is gated on `ctx->cache_dir` (i.e. `--build-cache` must be
passed) — without it, every step rebuilds unconditionally.

A **`CcccExecutable`** target has no per-source `.o` and no host `cc` to
pass `-MMD` to, so under `--build-cache` the builder instead passes
`cccc --compile=native --deps-file=<objdir>/<target>.d`. `cccc` writes the
set of files its front end opened — the sources plus every `#include` it
resolved from disk — and the builder folds those paths' *content* into the
target's single per-target cache key, and mtime-checks them the same way
Level 1 checks a `.d`'s prerequisites. A target with no `.d` yet (first
build) is never trusted by mtime alone. The upshot: editing a header a
`CcccExecutable` source `#include`s invalidates the cached binary with no
`AddInput` declaration. A **`CcccObject`** caches the same way.
