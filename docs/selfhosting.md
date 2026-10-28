# Self-Hosting

Build cccc with itself: `cccc -c=native` compiles cccc's own sources into a working `cccc`. This is a manual check, run when touching serializer or parser completeness; it is not wired into CI.

## Recipe

Run it from the repository root, because `src/std_table.c`'s `.incbin` paths are relative to the working directory.

```sh
SDK=$(xcrun --show-sdk-path)
cccc --use-system-headers -c=native \
  $(ls src/*.c src/stdlib/*.c | grep -v -e src/ops.c) \
  -I include -I src -i "$SDK/usr/include" -I "$SDK/usr/include/ffi" \
  --iframework "$SDK/System/Library/Frameworks" \
  --link-arg=-framework --link-arg=CoreServices \
  -l pthread -l iconv -l ffi -L /opt/homebrew/opt/libffi/lib \
  -o /tmp/cccc_selfhost
```

| Platform | Changes |
|---|---|
| Linux | Drop `-i`, `--iframework`, the ffi `-I` and the CoreServices link args; `pkg-config --cflags libffi` covers libffi as `Makefile` does; drop `-l iconv` (glibc has it in libc). `src/watch.c` builds against inotify |
| macOS | Keep `--use-system-headers`: `src/watch.c` (with `src/dmon.h`) includes CoreServices, whose SDK headers cannot share a unit with CCCC's bundled `<signal.h>` and `<sys/*.h>`. Without it every file but `src/watch.c` compiles and the link fails on `cc_watch_*` |

`-i` is CCCC's `--isystem` short flag, not `-isystem` (getopt would read that as `-i` with the argument `system`). `--sysroot` and `-i "$SDK/usr/include"` are equivalent here.

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
