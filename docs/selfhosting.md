# Self-Hosting

Build cccc with itself: `cccc -c=native` compiles cccc's own sources into a working `cccc`. This is a manual check, run when touching serializer or parser completeness; it is not wired into CI.

## Recipe

Run the `ouroboros` build target from the repository root, because `src/std_table.c`'s `.incbin` paths are relative to the working directory:

```sh
./cccc --build build.c --build-target=release     # build/cccc-release
build/cccc-release --build build.c --build-target=ouroboros
```

The result is `build/cccc-ouroboros`; the target runs its `--version` as a smoke check.

| Platform | The target sets |
|---|---|
| macOS | The SDK (`xcrun --show-sdk-path`) as system headers and framework path, `CoreServices`, `iconv`, libffi from Homebrew |
| Linux | libffi flags from `pkg-config`, plus `/usr/include` and the multiarch include directory. `src/watch.c` builds against inotify. Not verified: in a 4 GiB aarch64 container the build exhausts memory (#1878) |

`--use-system-headers` is set on macOS: `src/watch.c` (with `src/dmon.h`) includes CoreServices on macOS, whose SDK headers cannot share a unit with CCCC's bundled `<signal.h>` and `<sys/*.h>`. Without it every file but `src/watch.c` compiles and the link fails on `cc_watch_*`.

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
