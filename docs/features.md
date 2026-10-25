# Features

CCCC is a C bytecode compiler and VM that acts as a drop-in frontend for `cc`/`clang`/`gcc`.

## Language

| Feature | Details |
|---|---|
| **Compile-time macros** | `@comptime` functions run during compilation: rewrite call sites, generate functions, globals and types ([macros.md](macros.md)) |
| **Derive attributes** | `@serialize`, `@to_json`, `@to_string`, `@equals`, ... generate code for a type ([derive.md](derive.md)) |
| **Attributes** | GNU `__attribute__`, C23 `[[...]]` and the `@name` shorthand ([attributes.md](attributes.md)) |
| **Operators and methods** | `[[cccc::operator(+)]]` and `[[cccc::method(T)]]` bind operators and `s.f(x)` calls to functions on structs, written free or in the struct body ([operators.md](operators.md), [methods.md](methods.md)) |
| **C23 and extensions** | C89 through C23, GNU and MS extensions ([coverage.md](coverage.md)) |
| **Decimal floating point** | `_Decimal32/64/128` via the Intel BID library, opt-in ([install.md](install.md)) |
| **Kernels** | `[[cccc::kernel]]` and OpenCL C 1.2 over a grid of work-items; `-fopenmp` ([kernels.md](kernels.md), [opencl.md](opencl.md), [openmp.md](openmp.md)) |

## Tooling

| Feature | Details |
|---|---|
| **Testing** | `@test` and `Assert*` in the same file as `main()`, TAP output ([testing.md](testing.md)) |
| **Build system** | `--build` scripts written in C, with targets, profiles, cross-compilation and caching ([build.md](build.md)) |
| **Debugger** | GDB-like breakpoints, watchpoints, edit-and-continue, debug-on-crash ([debugger.md](debugger.md)) |
| **REPL** | Declarations persist across the session; expressions print a typed result ([repl.md](repl.md)) |
| **Watch** | `--watch` reloads a program or rebuilds on save ([watch.md](watch.md)) |
| **Config files** | `.cccc.toml` with a trust prompt ([config.md](config.md)) |
| **Warnings** | gcc/clang-style `-W` categories and `-Werror` ([warnings.md](warnings.md)) |
| **URL includes** | `#include <https://...>` and `#embed <https://...>`, opt-in ([headers-native.md](headers-native.md)) |
| **JSON reflection** | `--ffi-decls` dumps declarations for FFI wrapper generators |

## Safety

| Feature | Details |
|---|---|
| **Safety levels** | `-0` to `-3`, from zero overhead to maximum checking ([safety.md](safety.md)) |
| **Detectors** | Use-after-free, overflows, dangling pointers, uninitialized reads, integer overflow, CFI ([safety-memory.md](safety-memory.md), [safety-pointers.md](safety-pointers.md)) |
| **Checked pointers** | Bounds carried by the pointer declaration ([safety-checked-pointers.md](safety-checked-pointers.md)) |
| **Sandboxing** | `--sandbox` and comptime policy for untrusted code ([security.md](security.md), [comptime-policy.md](comptime-policy.md)) |

## Output

| Feature | Details |
|---|---|
| **Native compilation** | `-c=native` hands the compiled program to a host compiler as portable C ([native.md](native.md)) |
| **Source maps** | JSON export for IDE integrations ([debugger-source-map.md](debugger-source-map.md)) |
