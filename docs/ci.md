# CI and Architecture Workflows

How each platform is built and tested.

## Architecture build and test workflows

> The repo-root `Makefile` builds `./cccc` (see [install.md](install.md)). Every
> staged `<platform>-build`/`-smoke`/`-test` workflow below and
> `linux-x86_64-msan-test` has a `--build-target=NAME` equivalent in
> `build.c` — see each subsection.

### Linux/amd64 with VZ/Rosetta

The root Dockerfile builds Ubuntu 24.04 with Clang, libffi, and `file` for
both arm64 and amd64, running `make`
inside the image (mirroring `.builds/linux-amd64.yml`). On Apple Silicon,
VZ/Rosetta is the supported amd64 path. Create the profile once:

```bash
colima start cccc-linux-amd64 --runtime containerd --arch aarch64 \
  --vm-type vz --vz-rosetta --cpu 4 --memory 4
```

Then run the staged workflow (each target requires that profile running; none
of them alter its lifecycle):

```bash
./cccc --build build.c --build-target=linux_amd64_build    # nerdctl build --platform linux/amd64
./cccc --build build.c --build-target=linux_amd64_smoke    # + uname/file/exit-42 check
./cccc --build build.c --build-target=linux_amd64_test     # + the `test` target in the container
```

The image is built and run with `--platform linux/amd64` and tagged
`cccc-linux-amd64`. `linux_amd64_smoke` requires `uname -m` to report
`x86_64` and `file ./cccc` to identify an x86-64 ELF executable (delegated to
`tools/linux_container_smoke.sh`, shared with the aarch64 side below).
`linux_amd64_test` depends on `linux_amd64_smoke` and then runs
`./cccc --build build.c --build-target=test --build-jobs=8 --build-keep-going`
inside the container under a 600s `timeout` (delegated to
`tools/linux_amd64_test.sh`). All
three targets accept optional positional args (profile, image, jobs) if you
need to override the `cccc-linux-amd64` / `8` defaults — see each script's
header comment in `tools/`.

**Gotcha:** the `cccc-linux-amd64` Colima *VM* is itself started with
`--arch aarch64` (Apple Silicon cannot run an x86_64 VM natively) — x86_64
only appears one layer down, inside a `--platform linux/amd64`
container/image run *inside* that VM via VZ-Rosetta binfmt. `colima ssh -p
cccc-linux-amd64` drops into the VM host, not that container, so `uname -m`
there legitimately reports `aarch64` — that is not a misconfiguration. Only
`uname -m` run *inside* the `--platform linux/amd64` image (as
`linux_amd64_smoke`/`linux_amd64_test` do, or `colima -p cccc-linux-amd64
nerdctl -- run --platform linux/amd64 cccc-linux-amd64 uname -m`) is expected
to report `x86_64`. Ad-hoc verification probes (e.g. offsetof/sizeof checks
against a real header) that just `colima ssh` into this profile are silently
checking aarch64 twice, not x86_64 — run them inside the
amd64-platform container instead.

**Not a perfect proxy for sr.ht:** a Rosetta-emulated x86_64 container is
the only *local* way to reproduce Linux-specific behavior, but it is not
guaranteed to match sr.ht's own build environment's glibc exactly. Directly
observed during an earlier fix: the `test_serialize_*` round trips
passed inside this container, then failed on the very next real sr.ht push
with a distinct (if related) glibc symbol collision the container's own
glibc version apparently didn't trigger. Treat a container-green result as
"passes on at least one Linux/glibc combination," not "passes on sr.ht" —
the real push is still the acceptance test, not a substitute for it. Also, the old Python runner hung once inside this same container under
`-j8` (a thread deadlock, not reproduced natively on macOS or confirmed on
sr.ht's own hardware) — a second, independent reason a container run isn't
interchangeable with a real push.

#### MemorySanitizer (MSan)

MSan is Linux-only (clang, not available on macOS). This target builds
`cccc-msan` (`-fsanitize=memory`, via `--build-target=cccc_msan`) inside the
existing container and then runs the `test_msan` target (the suites against that binary) in the
container:

```bash
./cccc --build build.c --build-target=linux_amd64_msan_test
```

MSan catches uninitialized reads that ASan cannot — use it to investigate latent
UB in code paths that only manifest under specific heap/ASLR layouts.

**Known limitation:** `linux_amd64_msan_test` reports widespread
use-of-uninitialized-value false positives (hundreds of tests, spanning
unrelated areas — pthread, macros, math, sockets, minilua) inside the VM
interpreter dispatch loop itself (e.g. `op_JZ3_fn`, `ld_i32`/`op_LDR_W_fn` in
`src/ops.c`). This is not a real bug in cccc: the `VirtualMachine` struct is
fully zeroed before execution (`src/vm.c`), and the actual cause is the
classic MSan blind spot — cccc links `-lffi` and calls into ordinary system
libc, neither of which is MSan-instrumented in the project's Docker image, so
legitimate writes made inside those uninstrumented calls read back as
"uninitialized" through MSan's shadow memory. Fixing this for real requires
building against an MSan-instrumented libc/libffi sysroot (see
[MemorySanitizerLibcxxHowTo](https://github.com/google/sanitizers/wiki/MemorySanitizerLibcxxHowTo)
for the general approach), which does not exist yet — until then,
`linux_amd64_msan_test` is excluded from release-gating and its (~262/700)
failures should be treated as expected noise, not investigated as
regressions.

### Linux/aarch64 native

Native 64-bit ARM Linux testing runs inside a Colima arm64 VM (no emulation,
no binfmt stall). Create the profile once:

```bash
colima start cccc-linux-arm64 --runtime containerd --arch aarch64 \
  --vm-type vz --cpu 4 --memory 4
```

Then run the staged workflow:

```bash
./cccc --build build.c --build-target=linux_aarch64_build    # nerdctl build --platform linux/arm64
./cccc --build build.c --build-target=linux_aarch64_smoke    # assert uname -m == aarch64; run test_fortytwo.c
./cccc --build build.c --build-target=linux_aarch64_test     # the `test` target in the container
```

`linux_aarch64_smoke` asserts `uname -m` is `aarch64` and `file ./cccc`
identifies an aarch64 ELF executable (same `tools/linux_container_smoke.sh`
script the amd64 side uses, parameterized differently). `linux_aarch64_test`
runs `./cccc --build build.c --build-target=test --build-jobs=8
--build-keep-going` in the container under a 600s `timeout`.

### Current results

**macOS arm64 (native):** the `test` target (every suite, VM and native) passes.

**Linux x86_64 (VZ/Rosetta, linux/amd64 container):** the `test` target
passes. The x86_64 `struct stat` layout fix corrects the field
ordering for glibc x86_64.

**Linux aarch64 (native arm64 container):** the `test` target passes at
parity with the macOS arm64 baseline.

No architecture-specific tests are silently skipped; native-pass skips are
limited to suites carrying `CCCC_NATIVE_SKIP` with an explicit reason.

## Continuous Integration

CI is split across two hosts, and by *when* it runs — on every push vs.
only at release time — because no single CI provider covers all three
release-target quadrants and per-push coverage of all three isn't the goal:

| What | Host | Manifest | Trigger |
|---|---|---|---|
| Linux amd64 build+test | sr.ht (`builds.sr.ht`) | [`.builds/linux-amd64.yml`](../.builds/linux-amd64.yml) | every push to `devel` (other refs exit in a guard task) — **canonical** |
| Doxygen docs build, publish to GitHub Pages | GitHub Actions | [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) | every push to `trunk` (build-only, no deploy, on `pull_request`) |
| Linux aarch64 / macOS arm64 build+test+package | GitHub Actions | [`.github/workflows/release.yml`](../.github/workflows/release.yml) | `v*` tag push only |

None of the three CI jobs above build with `CCCC_HAS_CURL=1` — every `cccc`
they build and test is a no-curl binary. `tests/test_url_include_basic.c`,
`test_url_embed_basic.c`, and `test_url_probes.c` are guarded on `#ifdef
__CCCC_HAS_CURL__` and fall through to a trivially-passing stub in that
configuration (see docs/headers.md's URL includes section), so the
curl-enabled code paths — including the URL `#include`/`#embed` fetch itself
and its `-c=native`/`-m`/`-c=generated` replay rewrite — have **no automated
coverage on either remote**. Verify them manually in a
`CCCC_HAS_CURL=1 ./cccc --build build.c --build-target=cccc` build before
relying on a change there.

`builds.sr.ht` cannot run arm64/aarch64 images at all (confirmed against the
raw compatibility matrix — every arch: arm64/aarch64 cell is empty — and
empirically, three different images all failed identically with "Image ..
is not available for arch .arm64."), which is why those two quadrants
live on GitHub Actions. They're only built and tested when a release is
tagged (`release.yml`'s `v*` trigger), not on every push — see
[docs/releasing.md](releasing.md) for the full release procedure. This is a
deliberate tradeoff: `trunk` gets continuous Linux amd64 coverage from
sr.ht, but a regression specific to aarch64/macOS is only caught when a
release is cut, not on the commit that introduced it.

Doxygen HTML docs (`docs` build target, docs/install.md) used to be a
`.builds/linux-amd64.yml` task (for a time), dropped after sr.ht's
`ubuntu/lts` image started shipping a doxygen version that hangs (rather
than warning) when `graphviz`'s `dot` binary is absent, timing out every
job's docs step regardless of what the commit touched. Docs generation now
lives entirely on the GitHub side instead, published to
<https://takeiteasy.github.io/cccc/> — `ci.yml`'s docs job installs
`graphviz` explicitly alongside `doxygen` to avoid the same class of
failure, and doesn't need a working `cccc` binary at all (Doxyfile's
`INPUT` is just the three public headers, so `doxygen Doxyfile` runs
standalone — no stage0 bootstrap step). `WARN_AS_ERROR` in the Doxyfile
still makes this a real completeness gate on `pull_request` builds, just
without a deploy step (a fork PR has no permission to publish Pages, and
an unmerged PR's docs shouldn't go live). `./cccc --build build.c
--build-target=docs` still works locally for anyone with
doxygen+graphviz installed.

The repo is mirrored to GitHub (`takeiteasy/cccc`) as a **manual second
git remote** — no deploy key, PAT, or other credential is stored in either
CI system, so nothing here can push on its own:

```bash
git remote add github git@github.com:takeiteasy/cccc.git   # one-time
git push mirror devel     # sr.ht: fires .builds/linux-amd64.yml
git push github trunk     # GitHub: fires .github/workflows/ci.yml (docs)
```

`trunk` is the only long-lived branch and the only one pushed to either
remote (see [Branching](../CLAUDE.md)) — both pushes are routine on every
ordinary push, not just at release time, since GitHub needs to stay
current for the docs it hosts.

Only sr.ht's `.builds/linux-amd64.yml` and GitHub's `release.yml` actually
build/test `cccc`; `ci.yml`'s docs job doesn't. sr.ht runs (`-j 8`):

```bash
make                            # one pass: ./cccc
./cccc --build build.c --build-target=test --build-jobs=8 --build-keep-going
```

GitHub's `release.yml` jobs (Linux x86_64, Linux aarch64, macOS arm64) run
`test_release` (the suites against the `-O2` release binary) and then
`test_extra` (all loose-test area targets plus `test_smoke`),
with `--build-jobs=4` — hosted runners have fewer cores than the sr.ht
builder, and parallel-load timing sensitivity is exactly what surfaced the flaky signal tests.
The area targets and smoke scripts therefore run only at release time, so run
the matching area target locally when touching that area.

`CC=clang`, `CCCC_BUILD_CC=clang`, and `CCCC_NATIVE_CC=clang` must all be set
explicitly for the `linux-aarch64` job (and are, in
`.github/workflows/release.yml`'s `env:`): Ubuntu's plain `cc` resolves to
gcc, which rejects `-std=c23` (though a `-c=native` compile — with no
explicit `--std=`, or with one — no longer *fails* outright on
such a host, since the `-std` ladder now probes both the
implicit-default and explicit-`--std=` cases; see native.md's
"Serialized-output divergences" section for the ladder it falls back
through). These are three separate compiler-selection mechanisms — `CC` for
`make` (plain `CC ?= cc` in the Makefile); `CCCC_BUILD_CC` for `./cccc --build build.c`'s own internal
compiler probe (`cccc_find_build_cc()` in `src/vm.c`), which compiles/links
`--build` targets including cccc's own release build; and `CCCC_NATIVE_CC`
for the *separate* probe (`cccc_find_native_cc()`) `-c=native` hands its
generated C to, exercised here by the native pass of the suites.
All three ignore `CC` (and each other) entirely and otherwise try `{cc,
clang, gcc}` on `PATH` in that order. `CCCC_BUILD_CC` and `CCCC_NATIVE_CC`
used to be one conflated env var (`CCCC_NATIVE_CC` alone drove both) until
they were split — pointing `CCCC_NATIVE_CC` at a different compiler family
to exercise `-c=native` used to silently retarget the compiler that builds
cccc itself too, and a real gcc pointed at `CCCC_BUILD_CC` used to fail to
link on macOS (53 duplicate ctype/wctype symbols, fixed by a guard
in `src/internal.h`). GitHub's
`ubuntu-24.04-arm` runner ships a pre-installed `cc` (→ gcc), which wins
that fallback unless overridden. macOS runners don't need any of the
three — Apple Clang is already the default `cc`.

### Known intermittent failures under heavy parallel load

A handful of `tests/suites/test_suite_posix.c` subtests (timing-sensitive
fork/signal/stat tests, including `test_posix_sigaction_siginfo`) can fail
under *severe* CPU contention — confirmed by directly reproducing it: running
8 copies of the compiled test binary concurrently on a resource-constrained
container reproduces the failure in roughly half of repeated trials, and
different subtests fail from run to run (not always the same one). This is
scheduler starvation, not a VM signal-dispatch defect — investigated and ruled out as a `_cccc_pending`/dispatch-loop drop path; a plain
sequential run never fails.

**Mitigation:** none in the harness; each suite is its own build target, so
reduce `--build-jobs` (or rerun the one suite) to remove contention. This
is a CI-determinism concern, not a fix to the underlying VM signal-dispatch
responsiveness under host load, which is tracked.

### macOS aio slot exhaustion under host contention

`tests/suites/test_suite_posix.c`'s `<aio.h>` tests
(`test_aio_sigev_thread`, `test_aio_sigev_signal`,
`test_aio_write_read_roundtrip`, `test_aio_fsync`, `test_aio_fsync_sigev_thread`,
`test_aio_cancel`, `test_lio_listio_wait`)
failed on GitHub's hosted macOS runners the first time this file's aio
coverage ran there — a release-time-only job, since macOS isn't in the
regular CI matrix (see [releasing.md](releasing.md)). Root cause, confirmed
on real macOS hardware with a host-native probe program: macOS caps
outstanding aio requests both per-process (`kern.aioprocmax`, 16 on a stock
host) and system-wide (`kern.aiomax`, 90), and a request keeps its slot even
after it *completes* until `aio_return()` reaps it, so `aio_write()`/
`lio_listio()` can fail synchronously with `EAGAIN` on a loaded shared host
through no fault of the guest or of CCCC (see the `<aio.h>` row of
[stdlib.md](stdlib.md) for the full writeup). This is not a CCCC bug and
is not emulated around — it's a real host limit.

**Convention used here, follow it for future host-limitation flakes:**
retry transient failures a bounded number of times (`aio_write_retry`/
`aio_read_retry`/`aio_fsync_retry`, 10 attempts at 20ms apart) rather than tolerating them
outright — a blanket pass would silently stop testing the code path. If
every retry is exhausted, fail loudly with the errno folded into the return
code (`100 + errno`) so the TAP `got N` line names it, since a `printf`
inside a test body is only shown in CI output for lines *at or after* the
`not ok` marker (the suite runner's failure-output window) — it's
invisible on both success and most failure formatting otherwise. One
exception: `test_aio_sigev_thread`'s `SIGEV_THREAD` submission tolerates a
*persistent* `EAGAIN` (after retries) as a pass, because some hosts reject
`SIGEV_THREAD` outright and there is nothing left to verify at that point —
that's a different, structural cause from transient slot contention, and
is long-standing.

`test_aio_slot_exhaustion` (macOS-only, `#ifdef __APPLE__`) deterministically
submits into slot exhaustion, asserts the failure is `EAGAIN`, reaps
everything, and confirms a fresh submission succeeds afterwards — proving
the reap-frees-slot property the retry logic above depends on, since the
other tests only exercise it nondeterministically depending on host load.

**Pitfall found while writing the retry helpers:** `usleep()` (used for the
retry backoff) can leave `errno` set to a stale value even when it
*succeeds* — confirmed with a plain host C program, `errno == ETIMEDOUT`
