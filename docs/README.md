# CCCC documentation

Each guide covers one topic. Hub pages (marked *hub*) link to their sub-pages.

**Start here**

| Guide | Covers |
|---|---|
| [getting-started.md](getting-started.md) | Install, first run, and the four ways to run a file |
| [features.md](features.md) | What CCCC does, one line per feature |
| [install.md](install.md) | Building `cccc` from source: dependencies, `make`, `CCCC_HAS_*` options |
| [config.md](config.md) | `.cccc.toml` files, the trust prompt, pinning an approval by hash |

**Compile-time macros**

| Guide | Covers |
|---|---|
| [macros.md](macros.md) | *hub* — `@comptime` functions, the return model, AST building and quoting |
| [derive.md](derive.md) | *hub* — attributes that generate code for a type: `@serialize`, `@to_json`, `@enum_to_string`, ... |
| [comptime-policy.md](comptime-policy.md) | What comptime code may call, and `--comptime-allow` |
| [security.md](security.md) | What runs or downloads while compiling, `--sandbox`, untrusted code |

**Testing and building**

| Guide | Covers |
|---|---|
| [testing.md](testing.md) | *hub* — `@test`, suites, assertions, hooks, filtering |
| [build.md](build.md) | *hub* — `--build` scripts: targets, profiles, cross-compilation |
| [watch.md](watch.md) | `--watch`: reload a program or rebuild when a file is saved |
| [repl.md](repl.md) | The interactive read-eval-print loop |
| [debugger.md](debugger.md) | *hub* — the source-level debugger, crash handling, the source-map API |

**Runtime safety**

| Guide | Covers |
|---|---|
| [safety.md](safety.md) | *hub* — safety levels `-0`…`-3` and the individual detectors |
| [warnings.md](warnings.md) | `-W` categories, `-Werror`, pragma suppression |
| [threading.md](threading.md) | Threads, the global lock, `<pthread.h>`, `<threads.h>`, atomics |

**Native output and headers**

| Guide | Covers |
|---|---|
| [native.md](native.md) | `-c=native`: lowering to portable C and handing it to a host compiler |
| [headers.md](headers.md) | *hub* — `#include` resolution, system headers, host-header hand-off |
| [inline-asm.md](inline-asm.md) | Inline `asm` in the VM, `--asm-passthru`, native hand-off |

**Parallel code**

| Guide | Covers |
|---|---|
| [kernels.md](kernels.md) | `[[cccc::kernel]]` over a grid of work-items |
| [opencl.md](opencl.md) | Reading OpenCL C 1.2 kernels |
| [openmp.md](openmp.md) | `-fopenmp` directives and `<omp.h>` |

**Reference**

| Guide | Covers |
|---|---|
| [attributes.md](attributes.md) | *hub* — `__attribute__`, `[[...]]` and `@name` support |
| [types.md](types.md) | Type compatibility, `_Generic`, `--compiler-family` |
| [coverage.md](coverage.md) | C89–C23 and extension support table |
| [stdlib.md](stdlib.md) | Standard-library and POSIX header support table |
