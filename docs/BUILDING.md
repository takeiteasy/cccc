# CCCC Build System (`--build`)

A Zig/Rust-style build system embedded in C. The build script is a normal `.c`
source file that CCCC compiles and runs at build time; the script declares
targets (executables, static libraries, dynamic libraries) and the CCCC build
runner compiles and links them with the system toolchain (`cc` / `ar` / `ld`).

There is no DSL and no separate config language: the build script is C, compiled
by the same preprocessor / parser / VM as every other CCCC source file. The mode
mirrors `--testing` (see [TESTING.md](TESTING.md)) and reuses the
`[[cccc::comptime]]` interception machinery (see [MACROS.md](MACROS.md)).

> v1 produces **native** output only. See [Scope](#scope) for what is deferred.

## Quick start

```c
// build.c
[[cccc::build]]
int build_main(cccc_build_ctx_t *ctx) {
    cccc_target_t *core = cccc_static_lib(ctx, "core");
    cccc_target_add_source(core, "src/lib/sum.c");
    cccc_target_add_include(core, "include");

    cccc_target_t *app = cccc_executable(ctx, "app");
    cccc_target_add_source(app, "src/main.c");
    cccc_target_add_include(app, "include");
    cccc_target_link_with(app, core);

    return cccc_build_run_default(ctx);
}
```

```sh
$ cccc --build build.c
[1/3] cc -c src/lib/sum.c -o build/obj/core/sum.o -Iinclude
[2/3] ar rcs build/lib/libcore.a build/obj/core/sum.o
[3/3] cc -c src/main.c -o build/obj/app/main.o -Iinclude
[3/3] cc -o build/bin/app build/obj/app/main.o -Lbuild/lib -lcore
build succeeded (2 targets, 0 errors)
```

A runnable example lives in [`examples/build_demo/`](../examples/build_demo).

## How it works

```
        cccc --build build.c
                 │
                 ▼
  1. Preprocess build.c (inject cccc_build.h)
  2. Parse; intercept [[cccc::build]]
  3. Resolve the build entry (flag / attribute / name)
  4. Compile the entry into the CCCC VM
  5. Invoke the entry inside the VM:
       ├─ entry calls factories → builds the cccc_target_t graph (data only)
       └─ entry calls cccc_build_run*(ctx)
  6. The host runner (native C, behind the FFI boundary) topologically sorts the
     graph and spawns cc/ar/ld (serial) to compile and link the targets.
  7. Exit with the entry's return value (non-zero = build failure).
```

The VM-side entry only *declares* data; the actual compilation happens host-side
in step 6. `cc`/`ar`/`ld` are spawned by the runner outside the VM, so they never
go through the VM's FFI layer and are not subject to FFI policy.

## CLI

```
cccc --build build.c                      # run the build entry in build.c
cccc --build build.c --build-entry=foo    # use `foo` as the entry
cccc --build build.c --build-out-dir=out  # output directory (default: build/)
cccc --build build.c --build-dry-run      # print command lines, run nothing
```

| Flag | Default | Meaning |
|------|---------|---------|
| `--build` | (off) | Switch to build mode. The input is a build script; `main()` is not required (and is rejected). |
| `--build-entry=NAME` | `build_main` | Symbol to invoke as the build entry. |
| `--build-out-dir=PATH` | `build/` | Output directory for artifacts. |
| `--build-dry-run` | off | Topo-sort and print the resolved command lines without executing them. |

Existing flags forwarded to every target's compile as defaults: `-I`, `-i`, `-D`,
`-U`, `--std=`, `-L`, `-l`. VM-only options (`-c`, `-d`/`--disassemble`,
`-O<n>`/`--optimize`, `--vm-profile`, `-g`/`--debug`, `-o`, `-E`, `-M`, `--ast`)
are rejected in `--build` mode.

The toolchain is selected via `CCCC_NATIVE_CC` (else `cc` / `clang` / `gcc`);
`ar` is selected via `CCCC_NATIVE_AR` (else `ar`).

## The build entry

The entry is the function the runner invokes. It is identified in precedence
order:

1. **Explicit flag:** `--build-entry=NAME`.
2. **Attribute:** a function tagged `[[cccc::build]]`. Exactly one is required;
   more than one is an ambiguity error listing every candidate.
3. **Default name:** a function named `build_main`.

The attribute accepts the C23 and GNU forms, like `[[cccc::comptime]]`:

```c
[[cccc::build]]                     // C23
__attribute__((build))              // GNU
__attribute__((cccc::build))        // GNU namespaced
```

Signature (the entry takes only the context):

```c
int  build_main(cccc_build_ctx_t *ctx);   // non-zero return = build failure
void build_main(cccc_build_ctx_t *ctx);   // success iff all targets built
```

The same file is still valid C: in default mode (`cccc build.c`) the
`[[cccc::build]]` attribute is consumed and the entry is simply not called.

## Target kinds

| Kind | Factory | Default output | Backend |
|------|---------|----------------|---------|
| Executable | `cccc_executable(ctx, name)` | `bin/<name>` | system `cc` |
| Static library | `cccc_static_lib(ctx, name)` | `lib/lib<name>.a` | `ar rcs` |
| Dynamic library | `cccc_dynamic_lib(ctx, name)` | `lib/lib<name>.{so,dylib}` | `cc -shared` |

## Builder API

All of the following are declared in the auto-injected `cccc_build.h`; do not
include it directly.

```c
// Build context
const char *cccc_build_root(cccc_build_ctx_t *ctx);     // launch directory
const char *cccc_build_out_dir(cccc_build_ctx_t *ctx);  // output directory
const char *cccc_build_host(cccc_build_ctx_t *ctx);     // "darwin" | "linux" | ...
int         cccc_build_verbose(cccc_build_ctx_t *ctx);

// Target factories — each returns a target owned by ctx
cccc_target_t *cccc_executable(cccc_build_ctx_t *ctx, const char *name);
cccc_target_t *cccc_static_lib(cccc_build_ctx_t *ctx, const char *name);
cccc_target_t *cccc_dynamic_lib(cccc_build_ctx_t *ctx, const char *name);

// Output / sources
void cccc_target_set_output(cccc_target_t *t, const char *path);
void cccc_target_add_source(cccc_target_t *t, const char *path);

// Flags
void cccc_target_add_include(cccc_target_t *t, const char *path);
void cccc_target_add_define(cccc_target_t *t, const char *name, const char *value);
void cccc_target_add_undef(cccc_target_t *t, const char *name);
void cccc_target_add_cflag(cccc_target_t *t, const char *flag);
void cccc_target_add_ldflag(cccc_target_t *t, const char *flag);

// Dependencies
void cccc_target_link_with(cccc_target_t *t, cccc_target_t *dep); // build before, -l<dep>
void cccc_target_add_lib(cccc_target_t *t, const char *name);     // -l<name>
void cccc_target_add_libpath(cccc_target_t *t, const char *path); // -L<path>

// Run (synchronous; returns 0 on success)
int cccc_build_run(cccc_build_ctx_t *ctx, cccc_target_t *t);  // t + its deps
int cccc_build_run_all(cccc_build_ctx_t *ctx);                // every target
int cccc_build_run_default(cccc_build_ctx_t *ctx);            // run_all + summary
```

`cccc_build_run*` compiles and links synchronously inside the call, so the
entry's return value reflects the real build status (this is why
`return cccc_build_run_default(ctx);` is the idiomatic last line).

## FFI policy

Build mode exists to call tools, so FFI is **allow-all by default**: a build
script may shell out to `pkg-config`, `hyperfine`, and so on without ceremony.
Passing `--ffi-allow=a,b,c` switches build mode into allowlist mode — only the
named functions are callable, everything else is blocked. The builder API
(`cccc_*`) and the host-spawned `cc`/`ar`/`ld` are the build runtime itself and
are always available regardless of `--ffi-allow`/`--ffi-deny`/`--disable-ffi`.

> The allowlist matches **C function names** (e.g. `system`, `popen`), not tool
> executables. Tool-name gating and toolchain probing are deferred (see below).

## Scope

**v1 (this release):** the `--build` mode, `[[cccc::build]]` entry resolution,
the auto-injected `cccc_build.h`, the three native target kinds, the core builder
API, a host-side **serial** runner with topological sort, `--build-out-dir`,
`--build-dry-run`, and the inverted FFI default.

**Deferred to later releases:** `[[cccc::build_target]]` discoverable factories;
`--build-target=NAME` selection; parallel `-j`; glob / `add_source_str` /
`exclude_source`; `cccc_probe_toolchain()` / pkg-config; `cccc_build_run_custom`;
bytecode targets; incremental / caching; cross-compilation; release/debug
profiles; a self-hosting `build.c` replacing the Makefile.

## See also

- [docs/TESTING.md](TESTING.md) — `--testing` mode, the working analog this mode mirrors.
- [docs/MACROS.md](MACROS.md) — the `[[cccc::comptime]]` system reused for interception.
- [docs/COVERAGE.md](COVERAGE.md) — the C surface a build script can use.
