# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.1.1] - 2026-08-07

### Added

- **CHKT3 shadow permutation for `qsort`/`bsearch`** — element movement
  through these libc callbacks is now tracked so out-of-bounds/type-mixing
  writes from a comparator or the reorder itself are still caught (see
  `man/SAFETY.md`)
- **`%n`-aware `printf` classification + CHKT3 shadow page reclamation** —
  `%n` write targets are classified against the shadow map, and shadow
  pages are reclaimed on sweep instead of growing unbounded (see
  `man/SAFETY.md`)

### Fixed

- CHKT3 FFI shadow backstop now clears global buffers too, not just heap
  allocations, closing a gap where a global passed through FFI kept a
  stale shadow entry
- Discarded-value loads (`(void)*p`) no longer use `REG_ZERO` as the load
  address, which could fault or silently no-op the bounds check depending
  on codegen path
- `reflection.h` no longer declares a duplicate `VirtualMachine` typedef

### Changed

- Public headers (`building.h`, `reflection.h`, `testing.h`) and the
  internal `src/cccc.h` fully migrated to pure Doxygen doc comments;
  `WARN_AS_ERROR` enabled now that header coverage is complete
- CI: `devel`/`main` integration and release branches retired — `trunk` is
  now the only long-lived branch (see this file's Branching section in
  `CLAUDE.md`); Doxygen docs now publish to GitHub Pages via
  `.github/workflows/ci.yml` on every push to `trunk`, and are no longer
  built as part of the sr.ht suite

## [0.1.0] - 2026-08-05

First release. There is no prior version to diff against, so this entry
summarizes the feature surface rather than a set of changes.

### Added

- **Compile-time macros** — `[[cccc::macro]]`/`__attribute__((macro))`/`@macro`
  functions that run during compilation, with quasi-quoting, hygienic
  reflection, and AST construction helpers (see `man/MACROS.md`)
- **Native compilation pipeline** — `-c=native` hands CCCC-preprocessed C to
  a real system compiler (`CCCC_NATIVE_CC`, `cc`, `clang`, or `gcc`) for a
  production build with no VM overhead
- **Register-based bytecode VM** — 32 integer + 32 floating-point registers,
  a portable instruction set, and a built-in interpreter (see `man/VM.md`)
- **Memory safety suite** — four preset levels (`-0` through `-3`) covering
  use-after-free, buffer overflows, dangling pointers, uninitialized reads,
  integer overflow, CFI, and more (see `man/SAFETY.md`)
- **Interactive debugger** — breakpoints, watchpoints, register/memory
  inspection, and a source map export API (see `man/TOOLING.md`)
- **Interactive REPL** — `-r`/`--repl`, incremental compilation, multi-line
  continuation, session commands
- **Bytecode optimizer** — `-O[N]`/`--optimize[=N]` levels 1-4 with
  individually toggleable passes (see `man/OPTIMIZATION.md`)
- **URL includes** — `#include <https://...>`, optional (`CCCC_HAS_CURL=1`)
- **Decimal floating-point** — `_Decimal32/64/128` via the Intel BID library,
  optional (`CCCC_HAS_DECIMAL=1`)
- **Built-in test framework** — `[[cccc::test]]`, `Assert*` macros, TAP output
  (see `man/TESTING.md`)
- **Attribute support** — GNU `__attribute__`, C23 `[[...]]`, `@name`
  shorthand (see `man/COVERAGE.md`)
- **JSON reflection output** — `--ffi-decls` dumps declarations for FFI
  wrapper generation
- **VM heap** — intercepting allocator on by default at every safety level
- **`--version`** — prints version, git describe, host triple, and enabled
  build features
- **Release build mode** — `./cccc --build build.c --build-target=release`
  (host `-O2 -g -DNDEBUG`), distinct from CCCC's own guest-side
  `--optimize`/safety levels
- Supported platforms: macOS and Linux, aarch64 and x86_64

[0.1.0]: https://git.sr.ht/~takeiteasy/cccc
