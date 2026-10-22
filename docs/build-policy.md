# Build Policy and Limits

Watching, bootstrapping, the tool allowlist, FFI policy and what is not supported.

## Watching

`cccc --watch build.c` runs the build again whenever a source, header or other
input changes. The script can add to the watch set with `Watch`, `WatchGlob` and
`IgnoreWatch`, and ask `BuildWatching`. See [watch.md](watch.md).

## Bootstrapping

`make` builds `./cccc`; `--build` then drives everything else for CCCC itself.
That path and the full target list `build.c` exposes are covered in
[install.md](install.md).

## Tool allowlist (`--build-tool-allow`)

By default, build mode allows all tools to be probed and run. Passing
`--build-tool-allow` switches to allowlist mode: only named tools may be used
by `HaveTool`, `PkgConfig`, or `RunCustom`. Comma-separated names or repeated
flags both work:

```sh
cccc --build build.c --build-tool-allow=pkg-config,python3
cccc --build build.c --build-tool-allow=pkg-config --build-tool-allow=python3
```

The native build-runtime tools (`cc`/`ar`/`ld`) are invoked directly by the
host runner via `fork`+`execvp` and are never affected by this list.

## FFI policy

Build mode exists to call tools, so FFI is **allow-all by default**: a build
script may call any native function without ceremony. Passing `--ffi-allow=a,b,c`
switches build mode into FFI allowlist mode — only the named C functions are
callable. The builder API (`__builtin_build_*` / PascalCase macros) and the
host-spawned `cc`/`ar`/`ld` are the build runtime itself and are always
available regardless of `--ffi-allow`/`--ffi-deny`/`--disable-ffi`.

For gating which **tool executables** may be probed or run via `RunCustom`,
use `--build-tool-allow` (see above).

## Not yet supported

Everything the builder API declares is implemented for all target kinds. A
`CcccExecutable` target compiles guest programs with `cccc`, but `cccc`'s own
`build.c` does not yet build `cccc` that way — full self-hosting is still
future work. There is no on-disk bytecode-target output, and idle `-j` slots
are not redistributed between a parallel target build and its children (see
[Parallel builds](build-incremental.md#parallel-builds)).

## See also

- [install.md](install.md) — building `cccc` itself (`make` and the `build.c` targets).
- [watch.md](watch.md) — `--watch`: rebuild on save.
- [testing.md](testing.md) — `--testing` mode, the working analog this mode mirrors.
- [macros.md](macros.md) — the `[[cccc::comptime]]` system reused for interception.
- [coverage.md](coverage.md) — the C surface a build script can use.
- [native.md](native.md) — `-c=native` lowering scope and limitations; the
  backend a `CcccExecutable` target uses.
