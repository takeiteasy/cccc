# CI and Architecture Workflows

How each platform is built and tested. `make` builds `./cccc` ([install.md](install.md)); each workflow below has a `--build-target=NAME` equivalent in `build.c`.

## CI hosts

CI is split by host and by when it runs, because no one provider covers all three release quadrants.

| What | Host | Manifest | Trigger |
|---|---|---|---|
| Linux amd64 build and test (**canonical**) | sr.ht | [`.builds/linux-amd64.yml`](../.builds/linux-amd64.yml) | Every push to `devel` (other refs exit in a guard task) |
| Doxygen build, link check, publish to GitHub Pages | GitHub Actions | [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) | Every push to `trunk`; build-only on pull requests |
| Linux aarch64 and macOS arm64 build, test and package | GitHub Actions | [`.github/workflows/release.yml`](../.github/workflows/release.yml) | `v*` tag push only |

sr.ht runs (`-j 8`) `make`, then `./cccc --build build.c --build-target=test --build-jobs=8 --build-keep-going`. `release.yml` runs `test_release` (the suites against the `-O2` binary) then `test_extra` (all area targets plus `test_smoke`) with `--build-jobs=4`, since hosted runners have fewer cores and parallel-load timing is what surfaced the flaky signal tests. Area targets and smoke scripts therefore run only at release time: run the matching area target locally when touching that area. The release procedure is in [releasing.md](releasing.md).

| Fact | Consequence |
|---|---|
| `builds.sr.ht` cannot run arm64 images (every arm64 cell in its compatibility matrix is empty, and three images failed with "Image .. is not available for arch .arm64.") | Those quadrants live on GitHub, so an aarch64 or macOS regression is caught at release time, not on the commit that caused it |
| No CI job builds with `CCCC_HAS_CURL=1` | `test_url_include_basic.c`, `test_url_embed_basic.c` and `test_url_probes.c` are guarded on `__CCCC_HAS_CURL__` and become stubs, so URL include and embed fetching and their native replay have **no automated coverage**. Verify them in a `CCCC_HAS_CURL=1 ./cccc --build build.c --build-target=cccc` build ([headers-native.md](headers-native.md#url-includes)) |
| Doxygen left sr.ht | Its `ubuntu/lts` image ships a doxygen that hangs when graphviz's `dot` is missing. The GitHub `ci.yml` job installs both and needs no `cccc` binary (`Doxyfile`'s `INPUT` is the public headers). `WARN_AS_ERROR` makes it a completeness gate on pull requests, which never deploy. `./cccc --build build.c --build-target=docs` works locally |

The repo is mirrored to GitHub as a manual second remote; no credentials live in either CI system, so nothing pushes on its own. `trunk` and `devel` go to both:

```sh
git push origin trunk && git push mirror trunk
git push origin devel && git push mirror devel
```

### Compiler selection

Ubuntu's plain `cc` is gcc, which rejects `-std=c23`, and `ubuntu-24.04-arm` pre-installs it, so the `linux-aarch64` job sets all three variables (in `release.yml`'s `env:`). macOS needs none, since Apple Clang is the default `cc`.

| Variable | Selects | Default search |
|---|---|---|
| `CC` | The compiler `make` uses (`CC ?= cc` in the Makefile) | `cc` |
| `CCCC_BUILD_CC` | What `./cccc --build build.c` compiles and links with, including cccc's own release build (`cccc_find_build_cc()`, `src/vm.c`) | `cc`, `clang`, `gcc` in that order |
| `CCCC_NATIVE_CC` | What `-c=native` hands its generated C to (`cccc_find_native_cc()`), used by the native pass of the suites | Same |

The last two ignore `CC` and each other. They were once one variable, so pointing `CCCC_NATIVE_CC` at another compiler family silently retargeted the compiler that builds cccc itself. A `-c=native` compile on a gcc host no longer fails outright, because the `-std` ladder now probes the implicit-default and explicit `--std=` cases ([native-std.md](native-std.md)).

## Container workflows

### Linux amd64 (VZ/Rosetta)

The root Dockerfile builds Ubuntu 24.04 with Clang, libffi and `file` for arm64 and amd64, running `make` in the image (as `.builds/linux-amd64.yml` does). On Apple Silicon, VZ/Rosetta is the supported amd64 path. Create the profile once:

```sh
colima start cccc-linux-amd64 --runtime containerd --arch aarch64 \
  --vm-type vz --vz-rosetta --cpu 4 --memory 4
```

```sh
./cccc --build build.c --build-target=linux_amd64_build    # nerdctl build --platform linux/amd64
./cccc --build build.c --build-target=linux_amd64_smoke    # + uname/file/exit-42 check
./cccc --build build.c --build-target=linux_amd64_test     # + the `test` target in the container
```

| Target | Does |
|---|---|
| `linux_amd64_build` | Builds the image, tagged `cccc-linux-amd64`, with `--platform linux/amd64` |
| `linux_amd64_smoke` | Requires `uname -m` = `x86_64` and `file ./cccc` to show an x86-64 ELF (`tools/linux_container_smoke.sh`, shared with aarch64) |
| `linux_amd64_test` | Depends on smoke, then runs the `test` target inside the container under a 600 s `timeout` (`tools/linux_amd64_test.sh`) |

All three take optional positional arguments (profile, image, jobs) to override the `cccc-linux-amd64` and `8` defaults; see each script's header in `tools/`.

| Gotcha | Detail |
|---|---|
| The Colima VM is aarch64 | Apple Silicon cannot run an x86_64 VM. x86_64 appears only inside the `--platform linux/amd64` container, via VZ-Rosetta binfmt. `colima ssh -p cccc-linux-amd64` enters the VM host, where `uname -m` correctly says `aarch64`; ad-hoc probes there check aarch64 twice. Run them inside the amd64 container, e.g. `colima -p cccc-linux-amd64 nerdctl -- run --platform linux/amd64 cccc-linux-amd64 uname -m` |
| Not a perfect proxy for sr.ht | The container is the only local way to reproduce Linux-specific behaviour, but its glibc may differ from sr.ht's. Once the `test_serialize_*` round trips passed here and failed on the next sr.ht push with a different glibc symbol collision. Treat green as "passes on one Linux/glibc combination"; the real push is the acceptance test. The old Python runner also deadlocked once under `-j8` here, never on native macOS |

### Linux aarch64 (native)

A Colima arm64 VM, with no emulation or binfmt stall:

```sh
colima start cccc-linux-arm64 --runtime containerd --arch aarch64 --vm-type vz --cpu 4 --memory 4
./cccc --build build.c --build-target=linux_aarch64_build    # nerdctl build --platform linux/arm64
./cccc --build build.c --build-target=linux_aarch64_smoke    # uname -m == aarch64; run test_fortytwo.c
./cccc --build build.c --build-target=linux_aarch64_test     # the `test` target in the container
```

The smoke target checks `uname -m` and that `file ./cccc` shows an aarch64 ELF; the test target runs `test --build-jobs=8 --build-keep-going` under a 600 s `timeout`.

### MemorySanitizer

MSan is Linux-only (clang). The `linux-aarch64` release job runs `test_msan` on every tag; a failure blocks the release. `linux_amd64_msan_test` builds `cccc-msan` (`--build-target=cccc_msan`) in the container and runs `test_msan` against it. It catches uninitialized reads ASan cannot.

```sh
./cccc --build build.c --build-target=linux_amd64_msan_test
```

The host libc and libffi are not MSan-instrumented, so `cccc_call_native_function` (`src/ops.c`) marks host-call results, the bytes a host function writes through its pointer arguments, returned host allocations and guest-callback arguments as initialised.[^msan] In a container, run with `--security-opt seccomp=unconfined`; on a hosted runner, lower `vm.mmap_rnd_bits` to 28.

[^msan]: Stack buffers use a fixed 4096-byte window, so a larger buffer filled by an unclassified host function can stay partly poisoned (#1873).

### Current results

| Platform | `test` target |
|---|---|
| macOS arm64 (native) | Every suite passes, VM and native |
| Linux x86_64 (Rosetta container) | Passes |
| Linux aarch64 (native container) | Passes, at parity with macOS arm64 |

No architecture-specific test is silently skipped; native-pass skips are limited to suites carrying `CCCC_NATIVE_SKIP` with a reason.

## Known flaky tests

| Symptom | Cause | What to do |
|---|---|---|
| A few timing-sensitive `tests/suites/test_suite_posix.c` subtests (fork, signal, stat, e.g. `test_posix_sigaction_siginfo`) fail under severe CPU load, a different one from run to run | Scheduler starvation, not a VM signal-dispatch defect. Reproduced by running 8 copies of the binary on a small container (roughly half of trials); a sequential run never fails, and a dispatch-loop drop path was ruled out | Lower `--build-jobs` or rerun the one suite (each suite is its own target). No harness mitigation; the VM's signal responsiveness under host load is a known open item |
| `<aio.h>` tests (`test_aio_sigev_thread`, `_sigev_signal`, `_write_read_roundtrip`, `_fsync`, `_fsync_sigev_thread`, `_cancel`, `test_lio_listio_wait`) fail on GitHub's macOS runners with `EAGAIN` | macOS caps outstanding aio requests per process (`kern.aioprocmax`, 16) and system-wide (`kern.aiomax`, 90), and a request keeps its slot after completing until `aio_return()` reaps it. A real host limit, not emulated around ([stdlib.md](stdlib.md), `<aio.h>` row) | Follow the retry convention below |

### Convention for host-limitation flakes

Retry a bounded number of times rather than tolerating the failure outright, since a blanket pass would stop testing the path.

| Rule | Detail |
|---|---|
| Retry | `aio_write_retry`, `aio_read_retry` and `aio_fsync_retry`: 10 attempts, 20 ms apart |
| Exhausted retries | Fail loudly with the errno in the return code (`100 + errno`), so the TAP `got N` line names it. A `printf` in a test body is only shown for lines at or after the `not ok` marker, so it is invisible on success and most failures |
| Exception | `test_aio_sigev_thread` accepts a *persistent* `EAGAIN` after retries as a pass: some hosts reject `SIGEV_THREAD` outright and nothing is left to verify. That is structural, not slot contention |
| `test_aio_slot_exhaustion` (macOS only) | Submits into exhaustion, asserts `EAGAIN`, reaps everything and confirms a fresh submission works, proving the reap-frees-slot property the retries depend on |
| `usleep()` and `errno` | `usleep()` can leave `errno` stale (`ETIMEDOUT` was seen) even when it succeeds, so clear `errno` before testing it after a retry sleep |
