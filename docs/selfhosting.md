# Self-Hosting

Build cccc with itself: `cccc` compiles its own sources into a working `cccc`. This is a manual check, run when touching serializer or parser completeness; it is not wired into CI.

| Target | Output | Front end holds |
|---|---|---|
| `ouroboros` | `build/cccc-ouroboros` | One source at a time (`--compile=object` per file, then a cccc link) |
| `ouroboros_single` | `build/cccc-ouroboros-single` | Every source at once (one `-c=native` unit) |

## Recipe

Run the `ouroboros` build target from the repository root, because `src/std_table.c`'s `.incbin` paths are relative to the working directory:

```sh
./cccc --build build.c --build-target=release     # build/cccc-release
build/cccc-release --build build.c --build-target=ouroboros
```

The result is `build/cccc-ouroboros`; the target runs its `--version` as a smoke check. `ouroboros` compiles each of the 103 sources to `build/obj/ouroboros-<name>.o` (jobs run in parallel with `--build-jobs`) and links them through a `CcccExecutable` that holds `src/main.c`. `ouroboros_single` takes the same flags with every source in one unit, which needs several GB on Linux. `./cccc --build build.c --build-target=bench_memory` prints the peak memory of each route:

| Route | Peak memory (macOS) |
|---|---|
| `ouroboros_single` front end (`--ast` / `-m`) | 3.0 / 3.4 GB |
| `ouroboros`, largest file (`src/watch.c`) | 128 MB |

| Platform | The target sets |
|---|---|
| macOS | The SDK (`xcrun --show-sdk-path`) as system headers and framework path, `CoreServices`, `iconv`, libffi from Homebrew |
| Linux | glibc's headers (`/usr/include` and the multiarch directory) as system headers, libffi flags from `pkg-config`. `src/watch.c` builds against inotify. Verified on aarch64 in a 4 GiB container (`ouroboros_single`: cccc's own phases peak at about 3.6 GB) |

`--use-system-headers` is set on both platforms. On Linux, CCCC's bundled headers lack POSIX functions its own sources call (`strnlen`, `stpcpy`). On macOS, `src/watch.c` (with `src/dmon.h`) includes CoreServices, whose SDK headers cannot share a unit with CCCC's bundled `<signal.h>` and `<sys/*.h>`; without the flag every file but `src/watch.c` compiles and the link fails on `cc_watch_*`.

Building with the release `cccc` takes about 2 minutes on an Apple M1; see [benchmarks.md](benchmarks.md#compiling-cccc). A `make` build of `cccc` is about 3x slower.

## What to check

| Stage | Pass when |
|---|---|
| Build and link | The recipe completes |
| Smoke | The binary passes `--version`, `-E`, a VM run and a `-c=native` build |
| Suites | Every `tests/suites` suite passes in VM mode and its `-c=native` results match `build/cccc`'s |
| Loose tests | All `tests/test_*.c` and `tests/macros/test_*.c` pass |

`tests/host/test_watch_selfhost.c` builds `src/watch.c` next to a small driver and checks that a real file change is seen; `tests/host/test_sdk_headers.c` covers `va_list`, `__signed` and the SDK-only declarations under the SDK headers.

## Host values

The `src/stdlib/*.c` files that translate CCCC's canonical numbering to the host's (`_SC_*`, `_PC_*`, `_CS_*`, `SCHED_*`, `POLL*`, `LC_*`) would otherwise see CCCC's bundled headers under self-hosting, collapsing the tables to identity. `src/host_values.c` injects the real value as `__CCCC_HOSTV_<NAME>__` and `src/host_shadow_macros.h` restores it. A new translation needs an entry in both. See also [native-impl.md](native-impl.md#host-owned-layouts-under-self-hosting).
