# Parallel and Incremental Builds

Job control, the build cache and header dependencies.

## Parallel builds

`--build-jobs=N` shares one `-j` budget across two levels:

| Situation | What runs |
|---|---|
| Several targets ready | Up to N child processes build them at once; each compiles its sources serially, so at most N compiler processes run. Output interleaves, as with `make -j` |
| One target ready | It runs in-process with the full budget, up to N `cc -c` at a time |
| Non-POSIX or `--build-dry-run` | Always serial |

Idle slots are not redistributed: two targets under `-j4` get one source slot each.

## Toolchain

| Variable | Selects |
|---|---|
| `CCCC_BUILD_CC` (else `cc`, `clang`, `gcc`) | The compiler for `--build` targets, including cccc's own bootstrap |
| `CCCC_BUILD_AR` (else `ar`) | The archiver |
| `CCCC_NATIVE_CC` | Only the compiler `-c=native` hands its generated C to |

A real gcc works: `CCCC_BUILD_CC=/opt/homebrew/bin/gcc-16 ./cccc --build build.c --build-target=cccc`.

## Incremental builds

Everything here needs `--build-cache`; without it every step rebuilds. Each native compile also writes `-MMD -MF <objdir>/<stem>.d`, so the cache sees the headers a source includes.

| Level | Check |
|---|---|
| 1: mtime | A `.o` is current only if it is at least as new as its source *and* every header in its `.d`. A source with no `.d` yet is never trusted by mtime alone |
| 2: content hash | The cache key folds in the content of every header prerequisite, so a header edit changes the key |
| Link / archive | The step is skipped (`(up to date) <target>`) when the output exists, is at least as new as every object, and the argv hash in `<objdir>/.cccc-link` matches the current command |

A source with no `.d` in its objdir never touches the content-addressed store, because `--build-cache=PATH` is shared across objdirs while `.d` files are per objdir; a fresh objdir could otherwise hit an object compiled against different headers. The first compile in an objdir always runs, then stores under the key from the `.d` it just wrote.

### `CcccExecutable` and `CcccObject`

These have no per-source `.o` and no host `cc` to pass `-MMD` to. The builder instead runs `cccc --compile=native --deps-file=<objdir>/<target>.d`, which lists every file the front end opened. Their content goes into the target's cache key and is mtime-checked like Level 1, so editing an included header invalidates the binary with no `AddInput`. `AddInput` remains for data a comptime read pulls in without an `#include` ([build-sources.md](build-sources.md)).
