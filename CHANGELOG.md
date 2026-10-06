# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Version history
before the 0.1.0 reset is not relisted here — see the ticket tracker and
`git log` for the historical record.

## [Unreleased]

- Added: under `-2` and `-3`, a subscript directly over a fixed-size local or
  global array (`buf[k]`, `m[i][j]`, `s.arr[k]`) is bounds-checked at the
  access. `a[size]` and negative indices are reported; `&a[size]` and
  `a + size` stay legal. A struct's trailing array is treated as flexible.
- Fixed: `-x cl -m`/`-c=native` run outside the repo (no `-I`) no longer drops
  the OpenCL typedefs (`float4`, `uint`, ...) from the output.
- Fixed: `-c=native` no longer fails the layout assertion for a struct holding a
  32- or 64-byte vector under x86_64 gcc.
- Fixed: `p++`, `p--` and `p[-1]` on a pointer into stack or global memory no
  longer report "Negative array index" under `-2` and `-3`.
- Fixed: `--dangling-pointers` no longer reports a live frame's own array
  access (`buf[i]`, `m[i][j]`, `s.arr[i]`, including `+=` and `++`) as
  dangling when a returned frame's local once occupied that address.
- Fixed: locals and parameters with alignment above 8 (`_Alignas(N)`,
  `__int128`, wide `_BitInt`, wide vectors) have correctly aligned addresses
  in the VM, and `--dangling-pointers` tracks the aligned address.
- Fixed: `vector_size` types are aligned as the host ABI aligns them (capped
  at 16 on aarch64 and macOS), so locals, struct layout and `-c=native`
  agree.

## [0.8.0] - 2026-10-05

- Added: GNU `__auto_type` declares a variable typed from its initializer.
- Added: `-Wauto-declarator`, part of `-Wall`, flags an `auto` or `__auto_type`
  declaration that gcc rejects (a pointer declarator such as `auto *p = &x;`,
  or several declarators) under `--compiler-family=gcc`, the default. cccc
  accepts the declarations.
- Fixed: a declarator attribute after a C23 `auto` name
  (`auto t __attribute__((unused)) = a;`) is accepted.
- Fixed: a `cleanup` attribute on a variable-length array runs at scope exit
  and receives a pointer to the array, in the VM and under `-c=native`. A
  pointer-to-VLA local with both a `cleanup` and an initializer compiles under
  `-c=native` instead of failing with "redefinition".
- Fixed: the `cleanup` function of a `__block` variable receives the
  variable's own address in the VM.
- Fixed: a VLA declared in a `for` initializer (`for (int n = 3, v[n], i = 0;
  ...)`) compiles under `-c=native`, `-m` and `-c=generated` instead of
  failing with "cannot be serialized to C".
- Fixed: `-Wattributes` warns that `cleanup` does not apply to types in a cast,
  `sizeof` or `__typeof__` type-name, as gcc does.
- Fixed: `__alignof__(v)` and `_Alignof(v)` on a variable or struct member
  report its declared `aligned(N)` / `_Alignas(N)` alignment, not just its
  type's.
- Fixed: `-Wtautological-compare` no longer reports a self-comparison for
  operands that fold to a constant, such as `sizeof(struct P) == 8`.

- Fixed: subscripting a vector rvalue (`(a + b)[0]`, `f()[3]`) is accepted
  instead of failing with "not an lvalue". Assigning to such a lane is still
  an error.

- Fixed: `vector_size` after a pointer, array or function declarator
  (`int *q VS;`, `int b[2] VS;`, `int f(void) VS;`) vectorizes the innermost
  scalar, as in gcc. It was rejected as not a scalar type.

- Fixed: `vector_size` written before the type or after the type specifier
  (`[[gnu::vector_size(16)]] int v;`, `int __attribute__((vector_size(16))) v;`)
  now makes a vector, as in gcc. It was a parse error in declarations, and
  silently ignored in cast, compound-literal and `sizeof` type-names.

- Fixed: compile time no longer grows quadratically with the number of
  globals. A 54,000-line file with 6,000 functions compiles in 0.5 s instead
  of 5.5 s.
- Fixed: computing token columns no longer costs about 0.2 s on every compile
  that uses comptime.
- Fixed: GNU attributes that `__has_attribute` reports as recognized, such as
  `cold`, `hot`, `noinline` and `always_inline`, are accepted silently instead
  of warning "unknown attribute" under `-Wattributes`.
- Fixed: an unknown attribute after a function declarator warns once instead
  of three times.
- Fixed: an attribute after a parenthesised declarator, as in
  `int (*fp)(void) __attribute__((aligned(16)))`, is accepted instead of
  failing with "expected ','". Function attributes such as `format` and
  `nonnull` on a function pointer apply to calls through it.
- Fixed: `[[__gnu__::name]]` is accepted as `[[gnu::name]]`, reserved
  `__name__` spellings work inside `[[...]]`, and `__has_c_attribute(gnu::x)`
  reports recognized GNU attributes.
- Fixed: `-c=native` runs `cleanup` attributes instead of dropping them.
- Fixed: a `break`, `continue`, `return` or `goto` that leaves a scope before a
  cleanup variable's declaration no longer runs that variable's cleanup.
- Fixed: cleanup variables declared in a `for` header or inside a statement
  expression `({ ... })` are cleaned up.
- Fixed: a `cleanup` attribute applies only to the automatic variable that
  names it. A declaration typed from that variable (`__typeof__(v) t`) no
  longer inherits it.
- Fixed: a `cleanup` attribute at the start of a declaration
  (`__attribute__((cleanup(f))) int a, b;`, `[[gnu::cleanup(f)]] int a;`,
  `int __attribute__((cleanup(f))) *p;`) applies to every declarator instead
  of being dropped, and `int a, __attribute__((cleanup(f))) *p;` applies it to
  `p`.
- Changed: as in GCC, `cleanup` is ignored on a typedef, struct member,
  parameter, function, global or `static` local, with a `-Wattributes`
  warning. Static locals and variables of such a typedef no longer run it. A
  C23 `[[gnu::cleanup(f)]]` after the type specifier is ignored.
- Added: `--vm-profile` reports comptime execution in its own section, and
  works with `-c=native`, `-c=generated` and `-m`. With `--json`, the comptime
  profile is a nested `"comptime"` object.
- Fixed: `--vm-profile` no longer drops `main`'s counts when the program
  registers an `atexit` handler. The report no longer has a `cycles` line or
  JSON key; it counted only the last VM entry, so use `total_opcodes`.

## [0.7.3] - 2026-10-02

- Fixed: a `#pragma omp` inside a function body in an emit block no longer
  fails to compile. It now builds an OpenMP region, as it would in ordinary
  source, and is still kept in the emitted output.
- Fixed: `-Wunused` no longer reports a function label as unused when its only
  `goto` is spliced in from a `Quote()`/`QuoteLazy()` template.

## [0.7.2] - 2026-10-01

- Fixed: `-c=generated` no longer copies `#pragma omp` lines, `#include <omp.h>`
  or its guard into the output. A region in macro-generated code runs on the
  thread pool, and the runtime is emitted only when generated code needs it.
  `-m` and `-c=native` no longer replay `#pragma omp` at file scope.
- Added: `_Pragma("omp ...")` in a `Quote()` template builds an OpenMP region.
- Fixed: `-Wlogical-op` and `-Wtautological-compare` no longer fire inside
  `#if`/`#elif` expressions, such as those in the bundled `<stddef.h>`.

## [0.7.1] - 2026-10-01

- Fixed: a call to a single-return `static inline` function evaluated its
  arguments more than once, could report a spurious "not an lvalue", and could
  clobber the caller's locals. Such calls are now ordinary calls.

## [0.7.0] - 2026-10-01

- Added: OpenCL C 1.2 kernels. A `.cl` file, or `-x cl`, is read as OpenCL C:
  `__kernel`/`__global`/`__local`/`__constant`/`__private`, the `get_*_id` and
  size functions, `barrier`, `mem_fence`, the 1.2 atomics (including
  `atomic_xchg` on `float`), vector types of 16, 32 and 64 bytes with
  swizzles and vector literals, and `-cl-std=CL1.2`. See `man/OPENCL.md`.
- Added: `<cccc/kernel.h>` and `<cccc/opencl.h>` are embedded in the compiler,
  so they resolve from any directory without `-I`.
- Fixed: `auto`, `typeof_unqual`, `__atomic_fetch_*` and `_Atomic` compound
  assignment on a `[[cccc::global]]`, `[[cccc::local]]` or
  `[[cccc::constant]]` object gave the compiler's temporary that address space,
  and kernel code rejected it as "a global-memory object on the stack".
- Limitation: no vectors under 16 bytes (`float2`, `int2`, ...), no 128-byte
  vectors, no OpenCL built-in function library, no `half`.

## [0.6.0] - 2026-10-01

- Added: `-c=native` and `-m` run `cccc_launch`, the work-item builtins,
  barriers and `[[cccc::local]]` memory on CPU threads. Kernels without a
  barrier run their work-groups in parallel on the thread pool; kernels with a
  barrier run a thread per work-item. See `man/KERNELS.md`.
- Fixed: `-m` emitted a call to an undefined `__cccc_launch` for a kernel
  launch.
- Limitation: `--test-run` and `-c=generated` do not support `cccc_launch`; a
  native barrier kernel starts a thread per work-item for every group.

## [0.5.0] - 2026-10-01

- Added: `-fopenmp` parses `#pragma omp` (`parallel`, `for`, `parallel for`,
  `critical`, `atomic`, `barrier`, `single`, `master`, with `private`,
  `firstprivate`, `shared` and `reduction`) and the VM runs each region on one
  thread. `<omp.h>` provides the runtime functions. See `man/OPENMP.md`.
- Added: `lastprivate` on `for` and `parallel for`, and `copyprivate` on
  `single`.
- Added: `-c=native` and `-m` run OpenMP regions on real threads from a thread
  pool emitted into the output; `num_threads`, `if`, `critical`, `atomic`,
  `barrier`, `single`, `reduction` and the `omp_*` runtime work as in OpenMP.
- Fixed: the compare-and-swap loop behind `__atomic_fetch_<op>` started with a
  plain read that raced with other threads' updates.
- Limitation: native OpenMP has no `ordered`, `dynamic`/`guided` schedules,
  pointer loop variables, `atomic capture` blocks or Windows support;
  `copyin`, `linear` and `threadprivate` are not supported.

## [0.4.1] - 2026-10-01

- Fixed: the macOS release job failed on flaky tests. The SIGCHLD siginfo and
  aio submission tests in `test_suite_posix.c` are skipped on macOS.

## [0.4.0] - 2026-10-01

- Added: `<cccc/kernel.h>` runs `[[cccc::kernel]]` functions over a grid of
  work-items in the VM. `cccc_launch(kernel, CCCC_RANGE(...), CCCC_RANGE(...),
  args...)` takes a kernel function or function pointer, and checks the
  arguments like a call. See `man/KERNELS.md`.
- Added: work-item builtins (`cccc_global_id`, `cccc_local_id`,
  `cccc_group_id`, the size queries, `cccc_work_dim`) and `cccc_barrier`.
- Added: `[[cccc::local]]` objects in a kernel body and `CCCC_LOCAL(bytes)`
  parameters live in per-work-group memory, under the VM's bounds checks.
- Added: `--kernel-max-group-size=N` (default 256).
- Fixed: a thread that faulted was not reported by the thread runner.
- Limitation: `-c=native` rejects the launch and builtins; kernel arguments
  must be integers, pointers or floats (up to 8 of each kind).

## [0.3.2] - 2026-10-01

- Fixed: `--sysroot` and `--use-system-headers` failed with "expected ','" on
  glibc 2.43 and newer.
- Fixed: `BuildRoot()` returned a dangling pointer, so `DirExists` and
  `FileExists` on it failed intermittently.

## [0.3.1] - 2026-09-30

- Fixed: `defined(__has_c_attribute)`, `#ifdef __has_builtin` and the same
  test for every other `__has_*` operator were false. The operators are now
  visible to `defined`, `#ifdef` and `#ifndef`, and evaluate outside `#if`
  as well (`int n = __has_builtin(__builtin_expect);`).
- Fixed: `#include_next` failed with "cannot open file" after the including
  header had itself included another header. `#include_next` and
  `__has_include_next` now search from the directory after the one the
  current file was found in; `__has_include_next` no longer always returns
  `0`.
- Docs: the `[[cccc::kernel]]` guard in `man/ATTRIBUTES.md` uses a nested
  `#if defined(__has_c_attribute)`, which is valid in C++ and Metal too.

## [0.3.0] - 2026-09-30

- Checked arrays: `int a _Checked[10]` / `char s _Nt_checked[11]` make the
  declared extent part of the array's checked type, so indexing is
  bounds-checked against it and decay to an unchecked pointer carries the
  bounds along. Multidimensional and variable-length checked arrays are
  compile errors. See `man/SAFETY.md`'s "Checked Arrays" section.
- Bounds-safe interfaces: a call passing a checked pointer to a parameter
  with declared bounds is verified at the call site (`CHKAB`) that the
  argument's own bounds imply the parameter's. An annotated parameter needs
  no extra attribute and places no restriction on an unchecked caller.
- Fixed: the VM loaded a clobbered register when an operand of
  `atomic_store*`, `atomic_exchange`, `atomic_compare_exchange_*` or
  `ckd_add/sub/mul` contained a call (a store stored the byte offset; the
  exchange and compare-exchange forms crashed).
- Fixed: an assignment stored a wrong value when its lvalue's address
  expression contained a block call, a wide `_BitInt` operation or a
  statement-expression.
- Fixed: `-c=native` emitted an invalid comparison between an `_Atomic`
  scalar load and a constant (`(_Atomic unsigned)7`).


- Checked-pointer bounds casts: `[[cccc::assume]]`/`[[cccc::dynamic]]`, the
  entry point for converting an unchecked pointer into a checked one.
  Attach in a cast's type-name alongside the claimed checked kind/bounds
  form (cast-only — a compile error on a declarator). `assume` takes the
  claim on trust with no runtime check; `dynamic` verifies it against the
  source's own declared-checked bounds via the existing `CHKAB` opcode,
  with a compile error when the source has no verifiable bounds. Both
  desugar to a compiler-generated declared-checked local, so every existing
  checked-pointer pass (`CHKR`, propagation, `CHKAB`) sees an ordinary
  checked variable with no new resolution machinery. The sanctioned
  unchecked→checked conversion inside a checked region, exempt from its
  cast ban. Also new: `dynamic_check(cond)` /
  `__builtin_cccc_dynamic_check(cond)` (with `_Dynamic_check(cond)` as a
  Checked-C-compat alias, recognized only when no symbol of that name is in
  scope) — a programmer bounds assertion that traps via the new `CHKDC`
  opcode when `cond` is false, under `--checked-pointers`; a no-op
  otherwise. See `man/SAFETY.md`'s "Checked Pointers" § "Bounds casts" and
  § "`dynamic_check`".
- Checked regions: `[[cccc::checked]]`/`[[cccc::unchecked]]` on a function
  definition or compound statement, `#pragma cccc checked/unchecked
  begin/end`, and the Checked C keyword spellings `_Checked { ... }` /
  `_Unchecked { ... }` (recognized positionally, not as reserved keywords, so
  an existing identifier of either spelling is unaffected), require every
  pointer declared within their extent to be a checked kind
  (`single`/`array`/`ntarray`) and forbid casting to or between unchecked
  pointer types there — the incremental-migration half of the
  checked-pointer layer. Always-on compile-time diagnostics, independent of
  `--checked-pointers`; see `man/SAFETY.md`'s "Checked Regions" section.

## [0.2.0] - 2026-09-11

- Inline build recipes and tests alongside `main()`: a single `.c` file can
  now carry `main()`, `[[cccc::test]]` functions, and a
  `[[cccc::build]]`/`[[cccc::build_target]]` recipe together, each invoked
  by its own flag (`cccc`, `cccc --testing`, `cccc --build`). `--build` no
  longer rejects a script for defining `main()`; a file with
  `[[cccc::build_target]]` factories and no entry now runs all of them
  automatically under `--build`.
- New `--no-emit-tests` flag: drops `[[cccc::test]]` bodies from
  `-c=native`/`-c=generated` output entirely, instead of emitting them
  inert.
- `RunCustom` build-script shell: builtins now honour redirects and pipes,
  the last same-direction redirect wins, pipe file descriptors are
  `CLOEXEC`, multi-redirect and `$(...)` command substitution work, and a
  pipeline no longer leaks write-end file descriptors or deadlocks.
- `#if`/`#ifdef` `defined()`/`__has_*` operators are now recognized when
  revealed by macro expansion, not only in the raw source text; an
  unrecognized `__has_*` operator now gets its own diagnostic.
- `--sysroot` now resolves the correct multiarch include directory, and
  `asm-label`/attribute ordering under `--sysroot` is fixed.
- `sys/statvfs.h` gets a real-header hand-off (closing out the
  self-hosting type-shadowing audit).
- A URL `#include` now mirrors nested project-header includes it reaches,
  and its default cache directory is resolved before `--url-cache-clear`
  checks for one.

## [0.1.0] - 2026-09-06

- Initial release.
