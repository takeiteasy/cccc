# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- **Chained checked-pointer bounds propagation** — a local that is itself
  only propagated (never declared checked) can now act as a propagation
  source for a further candidate: `int *q = p + 2; int *r = q + 1; int *s =
  r + 1;` now enforces `s[i]` too, not just `q[i]`, chaining to arbitrary
  depth. Decided by iterating #919's whole-function eligibility rule to a
  fixpoint, seeded from declared-checked sources only and growing round over
  round, so an unrooted cycle (`q = r + 1; r = q + 1;`) never self-validates.
  A self-rooted reassignment (`q = q + 1;`) is now treated as neutral rather
  than poisoning `q`, matching the existing `q++`/`q += k` behavior. See
  [SAFETY.md § Checked Pointers](man/SAFETY.md#checked-pointers) (#941)

### Fixed

- **`CHKNT` now covers read-modify-write through an `[[cccc::ntarray]]`
  terminator slot** — `s[n] += 1`, `s[n]++`, `s[n]--` (and the `_Atomic`
  compare-and-swap RMW form) previously bypassed #923's null-terminator
  guard: the read-modify-write desugar's synthesized store never carried the
  checked-pointer bounds `CHKNT` keys off, so a non-null RMW into the
  terminator slot silently corrupted the invariant even though the
  equivalent direct assignment already trapped. See
  [SAFETY.md § Checked Pointers](man/SAFETY.md#checked-pointers) (#937)

## [0.2.1] - 2026-08-08

### Added

- **Checked-pointer bounds propagation across assignment** — `int *q = p +
  k;` now checks `q[i]` against a snapshot of `p`'s own absolute bounds
  taken at the assignment, instead of `q` (an ordinary unchecked pointer)
  getting no check at all. Sound under arbitrary control flow with no
  dataflow/join analysis: a local propagates only if its declaration and
  every subsequent assignment to it are checked-rooted, and `q++`/`q += k`
  preserve the snapshot since it's an absolute range. Composes with
  struct-member bounds below. See [SAFETY.md § Checked
  Pointers](man/SAFETY.md#checked-pointers) (#919)
- **Checked-pointer bounds on struct/union members** — a member's `count()`/
  `byte_count()`/`bounds()` may now name a sibling member (`struct S { int
  n; int * [[cccc::array, cccc::count(n)]] p; };`), resolved relative to
  whichever instance is actually accessed (`s.p[i]`, `sp->p[i]`, `(&s)->p[i]`,
  `(*sp).p[i]` all reach the same member-relative base). Previously a
  compile error. See [SAFETY.md § Checked
  Pointers](man/SAFETY.md#checked-pointers) (#921)
- **`CHKNT`: null-terminator guard for `[[cccc::ntarray]]`** — under
  `--checked-pointers`, a store of a non-zero value into an `ntarray` +
  `count(n)` pointer's widened terminator slot now traps. The presence half
  of the invariant (verifying a terminator actually exists somewhere in the
  declared range) is deliberately not enforced — `count(n)` on a Checked C
  `_Nt_array_ptr` is a lower bound, not an assertion of terminator presence,
  so a scan-based check would false-positive on conforming code and would
  itself require reading past the declared bound. See
  [SAFETY.md § Checked Pointers](man/SAFETY.md#checked-pointers) (#923)

## [0.2.0] - 2026-08-07

### Changed

- **`-G`/`--emit-generated` folded into `-c=generated`** — "serialize the
  runtime TU + macro-generated objects to C" is now a third `-c=FMT` target
  alongside `native` and `bytecode` (aliases: `gen`, `g`), instead of a
  standalone flag with its own `-o` semantics. `-G` is removed outright, no
  deprecated alias. `-c=generated` follows the same default-filename
  convention as the other two targets: `./a.gen.c` when `-o` is omitted
  (previously `-G` fell back to stdout). `--emit-only` and `--attr-target`
  are unchanged in name and semantics; they now apply to `-c=generated`
  (#936)
- **Bare `-c`/`--compile` now defaults to `native`** (was `bytecode`);
  `-c=bytecode`/`bc`/`c4` is the explicit spelling for the old default. Both
  `native` and `bytecode` now match `cc`/`clang`/`gcc`'s `a.out` convention:
  no `-o` writes `./a.out`/`./a.c4` respectively (previously `bytecode` fell
  back to stdout and `native` hard-errored without `-o`) (#932)

### Added

- **`--emit-cccc`** — preserves CCCC dialect syntax (`[[cccc::...]]`
  attributes, cccc-only `#include`s, checked-pointer qualifiers) in
  `-E`/`-m`/`-c=generated`/`-c=native` output instead of stripping it to
  portable C. With `-c=native`, disables the `cc`/`clang`/`gcc` PATH search
  in favor of an explicit `CCCC_NATIVE_CC` that understands the dialect
  (#933)
- **`--test-run[=LEVEL]`** — runs the program once under CCCC's VM safety
  instrumentation (`max` by default, or `none`/`basic`/`standard`/`max` /
  `0`/`1`/`2`/`3` like `--safety=`) and only proceeds to compile if that run
  succeeds (no crash, VM-detected safety violation, or hang; exit code is
  not checked). Implies `-c=native` when no `-c` is given. Runs in a forked
  child so the smoke test's post-execution VM state never leaks into a
  saved `-c=bytecode` artifact (#934)

### Fixed

- `usage()` (`src/main.c`) audit: dropped the long-dead `$publish`
  reference; `--inline-limit`'s documented default corrected (20, not 256);
  `-s`/`--std`'s documented default corrected (`gnu23`, not `gnu17`), added
  the already-supported `c89`/`c90`/`gnu89`/`gnu90` spellings, and replaced
  the stale "affects predefined macros only" note (`--std` now also gates
  tokenizer/preprocessor features); three CLI error messages that named
  `-M` (`--memory-leak-detection`) as an output mode instead of `-m`
  (`--dump-expanded`) corrected; `--trap-fp-divzero` moved out of "FFI
  Safety Options" (it has nothing to do with FFI) into "Optimization";
  documented the previously-missing `--link`, `--url-cache-dir`/
  `--url-cache-clear`, and `-I`/`-D`/`-U`'s long-form aliases

## [0.1.3] - 2026-08-07

### Changed

- `-c=native`/`-m`/`-G`: a VM-only safety/debug flag (`--checked-pointers`,
  `--bounds-checks`, `-g`/`--debug`, etc.) used to be a hard compile-time
  error under `-c=native` and a silent no-op under `-m`/`-G`. Both now warn
  and continue instead — these flags are genuinely inert in modes that hand
  off to the host `cc` or serialize plain C, not a real conflict — naming
  every ignored flag in the message. `#pragma cccc config(...)` keys are
  likewise silently dropped under `-c=native`; that now emits a
  `-Wignored-features` diagnostic (`-Wall`) at the pragma site instead of
  staying quiet (#924)

### Fixed

- `-G` serializer: reflection API file-scope anonymous globals built via
  `CompoundLiteral`/`InitArray`/`InitStruct` (`reflect_new_anon_gvar()`'s
  other two call sites, besides `MakeStringLiteral` which #925 already
  covered) no longer fall through as an undefined `.L..N` reference —
  `rename_anon_globals()` now runs under `-G` too, and the `-G` emit path
  forward-declares macro-generated globals ahead of any generated function
  body that references them
- `-c=native`/`-m`/`-G` serializer: pointer arithmetic whose result type is
  an array (e.g. a reflection `MakeSubscript` on an array-typed anon
  global) no longer casts to the array type itself (`(int [3])...`, invalid
  C) — casts to pointer-to-element instead
- `tools/testing/c4.py`: the c4 round-trip skip check tested for `-M`
  (`--memory-leak-detection`) instead of `-m` (`--dump-expanded`) when
  deciding whether a test's output mode was round-trip-incompatible — a test
  combining `-m` with the c4 suite silently mis-saved serialized C source as
  a `.c4` bytecode file instead of being skipped, then failed to reload

## [0.1.2] - 2026-08-07

### Fixed

- `-c=native`/`-m` serializer: anonymous globals (`new_anon_gvar`'s `.L..N`
  name) are no longer treated as opaque string literals across the board —
  static locals and compound literals (`(int[]){...}`, `&(struct S){...}`)
  now get a real, valid-C identifier and a proper `static` definition
  instead of an unreferenceable dotted name. Previously such a reference
  either emitted invalid C (a dotted identifier the host compiler rejects)
  or, worse, silently aliased the wrong data — e.g. a `static struct S`
  local compiled fine but read back as garbage bytes off a bogus
  string-literal global, the same severity class as #918's defect C
- `-c=native`/`-m` serializer: a function's hoisted local-variable
  declarations no longer collide when the same name is reused in sibling or
  nested blocks (e.g. two `for (int i = ...)` loops in one function, or an
  inner block shadowing a parameter's own name) — renamed on collision
  instead of emitting a duplicate declaration; a parameter itself is never
  renamed, since its signature is already committed to output by the time
  the collision check runs
- `-c=native`/`-m` serializer: a declaration-form `for` loop init
  (`for (int i = 0; ...)`) is no longer dropped as an unsupported
  expression — the loop variable is now actually initialized

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
