# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Fixed

- **The comptime declaration index no longer mis-names a declaration whose
  segment contains a fixed-size array inside an anonymous struct/union body,
  or a leading C23 attribute** — `segment_declarator_name()` (`src/macros.c`)
  finds a declaration's declared name by scanning forward for the first
  depth-0 `[` array-dimension group, but tracked `[`/`]` depth without
  tracking brace depth. A member array inside an anonymous struct/union body
  declared in the same statement as its own declarator (e.g. `typedef struct
  { char n[32]; } A;`) put that member's `[` at apparent depth 0, so the
  declaration was indexed under the member's name (`n`) instead of its own
  (`A`); a leading attribute (`[[deprecated]] int dx;`) hit the same `[` path
  with no preceding token, so the declaration was never indexed at all.
  Either way, something later needing the real name as a typename (e.g. using
  it as another struct's member type) failed to resolve it and misparsed
  with an unrelated `expected ','`. The scan now tracks brace depth (only
  treating a `[` as an array dimension at brace depth 0) and skips a leading
  `[[ ... ]]` attribute-specifier-seq as a unit. (#951)

## [0.2.3] - 2026-08-08

### Added

- **`CHKNT`'s null-terminator guard now covers `float`/`double`, `struct`/
  `union`, and wide `_BitInt`/`_Decimal` `ntarray` pointees** — previously
  only integer and pointer pointees were guarded (a deliberate v1
  exclusion, #923). Investigating the exclusion for a decision pass turned
  up an actual hole: `_BitInt(128)` passed the old `is_integer()` gate and
  set the guard flag, but its store lowers through codegen's memcpy branch,
  which returned before the old `CHKNT`-only emission site — so the flag
  was set and nothing was ever checked. `float`/`double` now reuse `CHKNT`
  itself (their value's raw bits are transferred into an integer register
  first). A new opcode, `CHKNTZ`, guards the memcpy-lowered pointees
  (struct/union, wide `_BitInt`, `_Decimal`) that never pass through a
  single value register: it scans the source bytes for any non-zero byte
  before the underlying `memcpy` runs, so the terminator slot is never
  actually clobbered when it traps. `long double` stays unguarded on
  purpose — its widened terminator slot is 16 bytes but the actual store is
  an 8-byte flat-double `FSTR`, so no opcode inspects its full stored
  representation. `CHKNTZ` only guards a whole-object store through the
  pointer itself; a member-wise write into the same slot (`tbl[n].a = 1;`)
  is a known, separately-tracked gap (#950). See
  [SAFETY.md § Checked Pointers](man/SAFETY.md#checked-pointers) and
  [VM.md § Safety Opcodes](man/VM.md#safety-opcodes) (#939)

### Fixed

- **`node_has_side_effects()` now sees through a ternary's branches** —
  `ND_COND` (the `cond ? then : els` ternary) stores its two branches in
  `->then`/`->els`, separately from `->lhs`/`->rhs`, and the side-effect
  check that gates checked-pointer bounds declarations
  (`resolve_bounds_tokens()`) and member object-expression instrumentation
  (`compute_checked_bounds()`, #921/#945/#947) never recursed into them —
  so `count(c ? i++ : 3)` was wrongly accepted, and `i++` would have run on
  every checked access instead of never. Now rejected at the declaration,
  same as `count(i++)`. See
  [SAFETY.md § Checked Pointers](man/SAFETY.md#checked-pointers) (#949)
- **GNU elvis (`a ?: b`) no longer forces a compiler temp when the
  condition is a plain, cheaply re-readable operand** — `a ?: b` always
  desugared to `tmp = a, tmp ? tmp : b`, whose `ND_ASSIGN` made a *pure*
  elvis bounds expression like `count(n ?: 8)` fail the check above even
  though it has no side effects. When `a` is an `ND_VAR`/`ND_NUM` and not
  `volatile`/`_Atomic`, the desugar now builds `a ? clone(a) : b` directly
  instead, which reads as side-effect-free; every other condition shape
  keeps the original temp-based desugar. See
  [SAFETY.md § Checked Pointers](man/SAFETY.md#checked-pointers) (#949)

### Changed

- **Checked-pointer bounds propagation and assignment-time bounds
  implication also evaluate a member object expression once per
  assignment** — `q = arr[k].p;` (bounds propagation) and `arr[k].p = src;`
  (assignment-time bounds implication) used to re-evaluate `k`'s indexing
  arithmetic 2-3 times while building that one assignment's bounds, the
  same duplication #945 already fixed for a direct per-access check. Both
  passes now share #945's hoist-into-a-temp treatment via a second temp
  allocator, needed because they run after their function's own local-list
  snapshot. Pure performance cleanup — same checks, same traps, no
  user-visible behavior change. See
  [SAFETY.md § Checked Pointers](man/SAFETY.md#checked-pointers) (#947)

### Documented

- **Why a side-effecting member object expression stays uninstrumented** —
  `f()->p[i]` is declined by checked-pointer bounds checking (no check
  emitted) because the hoist introduced by #945/#947 rewrites the *bounds
  expressions*, not the access itself: `f()->p[i]`'s own access still calls
  `f()` once regardless, so emitting a check would call it again just to
  build the hoisted object-expression temp — an extra evaluation of a
  side-effecting expression that a `--checked-pointers` build must never
  introduce over a default build. See
  [SAFETY.md § Checked Pointers](man/SAFETY.md#checked-pointers) (#948)

## [0.2.2] - 2026-08-08

### Added

- **Path-sensitive checked-pointer bounds propagation** — a propagation
  candidate that mixes checked-rooted and non-checked-rooted assignments
  (`int *q = malloc(...); if (c) q = p; q[i];`) is no longer poisoned to
  "never checked" for the whole function; it's classified "OPT" and its
  snapshot temps are refreshed at *every* assignment, rooted or not (a
  non-rooted store writes an explicit invalid sentinel instead of skipping
  the refresh), plus seeded with the sentinel at function entry. A new
  opcode, `CHKRO`, checks the snapshot but no-ops on the sentinel, so `q[i]`
  is enforced exactly on the paths where `q` actually holds a checked-rooted
  value at runtime — decided per executed path with no CFG/join/fixpoint
  analysis at all. Candidate registration also now covers an uninitialized
  declaration (`int *q;`), which previously never became a candidate.
  `checked_prop_optional` propagates transitively through a #941 chain, so a
  candidate chained from an OPT source is itself OPT even when its own
  single store is unconditionally rooted. A candidate whose every assignment
  is checked-rooted ("FULL") is completely unaffected — same `CHKR`, same
  codegen as before. See
  [SAFETY.md § Checked Pointers](man/SAFETY.md#checked-pointers) (#942)

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

### Changed

- **Struct/union member checked-pointer bounds evaluate their object
  expression once per access, not once per bound** — `arr[k].p[i]` used to
  re-evaluate `k`'s indexing arithmetic 2-4 times per checked access (once
  for `lo`, once or twice for `hi` depending on the bounds form), since
  building each bound re-cloned the member access's object expression
  (`arr[k]`) from scratch. A non-trivial object expression (a runtime
  index; not a bare local or plain member chain, which already cost
  nothing to re-clone) is now hoisted into a single compiler-generated
  temp shared by every bound of that access. Pure performance cleanup —
  same checks, same traps, no user-visible behavior change. See
  [SAFETY.md § Checked Pointers](man/SAFETY.md#checked-pointers) (#945)

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
