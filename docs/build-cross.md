# Cross-Compilation

Building for another target.

## Cross-compilation

Two mechanisms let a build script target a different architecture or OS than the
host.  They can be used independently or together.

**Clang-style triple (`--target`)** — use when your compiler accepts
`--target=<triple>` (clang, clang-cl).  Set once globally or per target:

```sh
# Global: all targets get --target=aarch64-linux-gnu
cccc --build build.c --build-triple=aarch64-linux-gnu

# Or per-target from inside the script:
SetTargetTriple(t, "aarch64-linux-gnu");
```

**Toolchain override** — use when your cross-compiler is a prefixed GCC binary
(e.g. `aarch64-linux-gnu-gcc`).  The override replaces the CC binary entirely;
`--target` is not added in this case.

```sh
# Global: every target uses the prefixed compiler
cccc --build build.c --build-cc=aarch64-linux-gnu-gcc

# Or per-target from inside the script:
SetToolchain(t, "aarch64-linux-gnu-gcc");
```

**Combining both** — set the toolchain for the compiler binary *and* the triple
for the clang-style `--target` flag when targeting a foreign sysroot with a
wrapper script that delegates to clang:

```c
SetToolchain(t, "clang");
SetTargetTriple(t, "aarch64-apple-macosx14.0");
```

**Precedence** for CC binary: `SetToolchain(t, …)` > `--build-cc` > system CC.
**Precedence** for triple: `SetTargetTriple(t, …)` > `--build-triple` (no triple = no `--target` flag).

`SetToolchain` and `SetTargetTriple` apply to `Executable`/`StaticLib`/
`DynamicLib` targets. To have a target compiled by `cccc` itself, use a
`CcccExecutable` target — not `SetToolchain(t, "cccc")`, which would hand
`cccc` a host-`cc` command line it does not accept. `SetTargetTriple` is
rejected on a `CcccExecutable` target.

**`BuildTargetTriple(ctx)`** returns the global triple set by `--build-triple`,
or `NULL` if none was passed.  Use it to conditionally add target-specific
sources or defines:

```c
const char *triple = BuildTargetTriple(ctx);
if (triple && strstr(triple, "aarch64"))
    AddCFlag(t, "-march=armv8-a");
```

> Cross-compilation tests use `--build-dry-run` to verify the correct flags
> appear in output without requiring a cross-toolchain on the host.
