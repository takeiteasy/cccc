# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Version history
before the 0.1.0 reset is not relisted here — see the ticket tracker and
`git log` for the historical record.

## [Unreleased]

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
