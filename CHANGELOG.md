# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Fixed

- **Subscripting a multi-dimensional VLA SIGSEGVed** (#971). `int v[n][m];
  v[1][2] = 42;` crashed both the stage0 and full build's VM. `v[1]` -- a
  row of the VLA, itself VLA-typed -- desugars to an `ND_DEREF` whose result
  type is `TY_VLA`; `gen_expr`'s `ND_DEREF` case (and its fused-load fast
  path, `emit_indexed_load_if_possible`) excluded `TY_ARRAY` from the
  trailing load but not `TY_VLA`, so the correctly-computed row address was
  overwritten by a load of `v[1][0]` before the outer subscript dereferenced
  it. Fixed by excluding `TY_VLA` the same way `TY_ARRAY` already is --
  deliberately *not* touching the sibling `ND_VAR` read path, which must
  keep loading a VLA variable's frame slot (it holds the alloca'd pointer,
  not the array data itself). The `-m`/`-c=native` serializer had a
  matching defect: a pointer to a VLA row (`int (*)[m]`) was mis-spelled
  `int *[m]` (array of pointers, invalid as arithmetic/pointer type) because
  `serialize_type_decl`'s `TY_PTR` branch only parenthesized for
  `TY_ARRAY`/`TY_FUNC` bases. VLAs of any dimension now round-trip through
  the VM and native output alike. Covered by
  `tests/suites/test_suite_vla.c`'s multi-dimensional cases,
  `tests/test_serialize_type_vla_2d.c`, and `tools/comptime_native_smoke.py`
  case 49.

- **Right-nested complex arithmetic computed the wrong value in the VM**
  (#968). `double complex z = 20.0 + 22.0 * I;` (parsed as
  `20.0 + (22.0 * I)`) evaluated to `19 + 0i` instead of `20 + 22i` --
  `-c=native` was correct the whole time, which is how the bug surfaced.
  `gen_complex_expr()`'s `ND_ADD`/`ND_SUB`/`ND_MUL`/`ND_DIV` case generated
  its right-hand operand into a fixed pair of registers, but the function
  recurses: a right-nested complex binop re-entered the same case and
  immediately reused those same registers for its own operands, destroying
  the outer right-hand value. Left nesting (`(a + b) + c`) survived only by
  accident. Fixed by having the right-hand operand's registers -- and, for
  `*`/`/`, its scratch registers -- come from the normal temp-register
  allocator, with the left-hand value's bits unconditionally spilled to the
  stack across the right-hand recursion. That spill also fixes a related
  bug found in the same pass (#970): a function call anywhere in the
  right-hand operand clobbered the left-hand value, since every temp
  register is caller-saved. Covered by
  `tests/suites/test_suite_floats.c:test_complex_nesting` and
  `tools/comptime_native_smoke.py` case 46.

## [0.2.8] - 2026-08-12

### Fixed

- **Serialized output silently dropped 14 node kinds** (#963). `-c=native`,
  `-m` and `-c=generated` routed any unhandled `NodeKind` to a `default:`
  arm that emitted a *comment* instead of failing. In expression position
  that broke the host build; in **statement** position it emitted
  `/* unsupported expr kind N */;` — a syntactically valid null statement —
  so the native binary compiled cleanly and silently returned a different
  answer than the VM (`__builtin_atomic_store(&x, 42)` simply vanished).
  Now serialized: the bit-manipulation builtins (`clz`/`ctz`/`popcount`/
  `parity`/`ffs`/`bswap`, including recovering the `ll` variants of
  `popcount`/`parity`, whose node encoding carries no width), the atomics
  (`__atomic_load_n`/`store_n`/`exchange_n`/`compare_exchange_n`, mirroring
  codegen's non-atomic fallback for float and odd-size pointees),
  labels-as-values and `goto *ptr`, `_Complex` construction and
  `creal`/`cimag`/`conj`, `__builtin_convertvector`,
  `__builtin_frame_address`, `__builtin_return_address`,
  `__builtin_dynamic_object_size`, the trap builtins, and `asm(...)`. Also
  closes the parallel *type*-serializer gap (`TY_VLA` — including its length
  expression, `TY_COMPLEX`, `TY_VECTOR`), which took the corpus from 34
  files emitting `/* unknown type */` to 3. Covered by
  `tests/test_serialize_{type,expr}_*.c` and, so the silent class is caught
  by *running* the binary rather than only building it, seven VM-42 →
  native-42 cases in `tools/comptime_native_smoke.py`. VLAs, the overflow
  builtins and blocks remain unserialized and keep their own tickets;
  divergences that cannot be avoided are documented in
  [COVERAGE.md](man/COVERAGE.md#serialized-output-divergences).

## [0.2.7] - 2026-08-12

### Fixed

- **Designated initializer on a field of an anonymous *union* member
  crashed the parser** (#960, follow-up to #489). `struct_designator()`
  (`src/parse.c`) special-cased anonymous *struct* members when resolving
  a `.name` designator but not anonymous unions, so `.i` in
  `struct S { union { int i; float f; }; int tag; } r = {.i = 7, .tag = 1};`
  fell through to a NULL `mem->name` dereference — reachable from a plain
  brace initializer, a compound literal, or a global, not just the
  compound-literal shape the ticket was found through. Fixed by matching
  the condition `get_struct_member()` already used
  (`TY_STRUCT || TY_UNION`), plus a defensive skip for any other nameless
  member. Also guards two adjacent `-Woverride-init` diagnostics that
  printed the same NULL `mem->name` for a re-initialized anonymous member.
  Covered by `tests/suites/test_suite_init.c`,
  `tests/test_warning_override_init_anon_member_960.c`, and the
  anonymous-union `return=` case in `tests/suites/test_suite_testing_framework.c`
  that #489 had to drop.
- **`-Woverride-init` never fired for a union member override** (#961,
  follow-up to #960). The ticket's own diagnosis (an anonymous-member
  `is_set` propagation gap) turned out to be only half right — an
  anonymous *struct* member already warned correctly. The real gap was
  that `designation()`'s `TY_UNION` branch (`src/parse.c`) performed no
  override check at all, which also silently affected a plain *named*
  nested union, not just an anonymous one. Fixed by adding the check
  there: since union members alias, it now also warns when a *different*
  member's designator overrides one already set (e.g. `{.i=1, .f=2}`,
  matching gcc/clang), naming whichever member was previously live. The
  two struct-side call sites (`designation()`'s `TY_STRUCT` branch and
  `struct_initializer1()`) now skip their own wrapper-level check for an
  anonymous struct/union member and let the recursive `designation()`
  call name the real leaf field instead, replacing the old generic
  "...anonymous member" message. Covered by
  `tests/test_warning_override_init_union_nested.c`,
  `tests/test_warning_override_init_union_diff_member.c`,
  `tests/test_warning_override_init_anon_struct.c`,
  `tests/test_warning_override_init_anon_positional.c`, and a rewritten
  `tests/test_warning_override_init_anon_member_960.c`.
- **Multiple designators in a single union initializer were a hard parse
  error** (#962, found while fixing #961). `union U u = {.i=1, .i=2};` or
  `{.i=1, .f=2};` failed with "expected '}'" — valid C accepted by
  gcc/clang, where later designators override earlier ones and the
  union's active member is whichever came last. `union_initializer()`
  (`src/parse.c`) parsed exactly one designator before demanding the
  closing brace; fixed by looping over `,`-separated designators,
  re-assigning the union's active member each time. Covered by
  `test_union_multi_designator_962` in `tests/suites/test_suite_init.c`.

### CI

- **The v0.2.7 release build failed on GitHub's macos-arm64 runner**
  (job 31596923436): `test_aio_sigev_signal`'s `aio_write()` retry budget
  (200ms, #929) was exhausted by host contention. Widened
  `aio_write_retry`/`aio_read_retry`/`aio_fsync_retry` in
  `tests/suites/test_suite_posix.c` to ~1s (40 attempts × 25ms) and bumped
  `test_aio_sigev_signal`'s timeout from 5000ms to 10000ms to match; see
  #929 (reopened with this evidence).

## [0.2.6] - 2026-08-12

### Added

- **`return=` compound-literal test assertions now support nested
  struct/union/array fields and anonymous struct/union members** (#489,
  follow-up to #353). A field that is itself a struct or union can be
  asserted with a nested compound literal (typed `(struct T){...}` or a
  bare `{...}` whose type is inferred from the field), recursing to a
  maximum of 8 levels; an array field takes a positional element list, or a
  `char[]` field can be compared against a string literal with C
  zero-initialisation semantics. An anonymous struct/union member's fields
  are addressed directly through the parent's own designators, matching
  C's anonymous-member lookup rules, rather than needing an extra level of
  nesting. Implemented as a recursive descent through both the parser
  (`parse_ret_init_list`, `src/preprocess.c`) and the comparator
  (`cmp_ret_aggregate`/`cmp_ret_value`/`cmp_ret_struct_body`,
  `src/testing.c`); malformed-literal recovery is brace-depth-aware so a
  bad nested field doesn't desynchronise the rest of the attribute's token
  stream. Also fixes a **latent union-comparison bug**: previously every
  member of a union was compared, with an omitted arm expected to be zero
  — but union arms alias the same storage, so an omitted arm doesn't mean
  "zero," it means "not this arm." Only members actually named in the
  literal are now compared for a union. Covered by new cases in
  `tests/suites/test_suite_testing_framework.c`'s `framework/struct_return`
  suite (nested/two-level nesting, nested `!=`, nested-omitted-expects-zero,
  arrays, array-of-struct, `char[]`-vs-string, anonymous struct member,
  malformed-nested-literal recovery, and the >8-levels depth cap). See the
  Struct / union section of [TESTING.md](../man/TESTING.md).

### Fixed

- Freeing a `return=` compound-literal's parsed field list previously had
  two separate implementations (`src/preprocess.c`'s error-recovery path
  and `TestFnRecord` teardown in `src/vm.c`); consolidated into a single
  `cc_free_ret_fields` so a nested field list can't be freed correctly in
  one path and leaked in the other.

## [0.2.5] - 2026-08-12

### Added

- **The interactive debugger's new `print`/`p <variable>` command formats a
  live local or global's value recursively instead of requiring a raw
  `memory <addr>` dump** (#958, follow-up to #666). Adopts the shared
  `cc_dump_value` formatter (`src/dump.c`) the REPL already used, printing
  scalars directly and struct/union/array/vector values field by field in
  the same lldb-style multi-line braces. This required first fixing
  `cc_is_valid_vm_address` (`src/debugger.c`): its stack-segment bound
  checked `[stack_seg, stack_seg + poolsize)`, the low end of the much
  larger `poolsize_max` reservation, which never actually holds committed
  data — the stack grows downward from the top, so every real local
  variable's address was being rejected as invalid. The REPL never hit this
  because its results always live in the RETBUF pool or data segment, never
  on the stack; a debugger `print` of a live local hits it immediately. Now
  validates against the stack's actual committed range,
  `[stack_base, initial_sp)`, which `vm_stack_grow` and `repl_init_stack`
  both already maintain correctly. Covered by new PTY integration tests in
  `tools/test_debugger_print.py` (struct, nested struct, array, `char*`
  string, pointer-to-local, data-segment global, and an unresolvable-symbol
  error path), wired into `tools/run_tests.py`'s unified suite as
  `debugger_print`. `man/TOOLING.md`'s Interactive Debugger section
  documents the new command and the stack-bound fix.
- **The interactive REPL now formats struct/union/array/vector expression
  results recursively instead of printing a placeholder** (#666). Previously
  a non-scalar result printed `<struct Point value: aggregate printing not
  yet supported>`; `repl_print_result` now delegates to a new shared
  `cc_dump_value`/`cc_dump_value_reg` formatter (`src/dump.c`, declared in
  `src/internal.h`) that prints lldb-style nested braces with depth/element
  caps, `char[]`-as-string rendering, bitfield extraction, and pointer
  validation — every pointer dereference (including nested inside a struct
  member) is checked against the VM's live segments via the debugger's
  `is_valid_vm_address` (now exposed as `cc_is_valid_vm_address`) before
  being followed, so an uninitialized/garbage pointer prints as hex instead
  of crashing. New PTY tests in `tools/test_repl.py` cover struct/union/
  array/vector results and nested structs with pointer members. Follow-up
  filed as #958 (debugger adoption of `cc_dump_value` for its inspect/print
  commands).
- **`examples/ccccl/`** — a trimmed copy of the `ccccl` project (a small
  Lisp that lowers to C entirely inside CCCC's comptime pass), added as a
  worked real-world example of `[[cccc::macro]]`/`Quote()` metaprogramming.
  Linked from `man/MACROS.md`.

### Fixed

- **`gen_expr`'s `ND_MEMBER` case could silently corrupt a live value when
  loading a `float`/`double` struct member** (#917). The member address was
  computed into `dest_reg` and then loaded through that same register; for a
  flonum member `dest_reg` is a float register, and `FREG_A0`-`FREG_A7`
  alias `REG_A0`-`REG_A7` by raw index, so a caller needing the address and
  the loaded value simultaneously live could have one silently overwrite the
  other. No observable bug prior to this fix (the address was always dead
  after the load), but the hazard is now closed by routing the address
  through a dedicated temp register, mirroring the pattern `ND_VAR`'s
  flonum branch already used. `man/VM.md`'s calling-convention section now
  documents the FREG/REG index aliasing; regression coverage added in
  `tests/suites/test_suite_floats.c` for flat/nested/pointer/array/union
  float member reads and a deep nested expression exercising peak temp-
  register pressure.
- **FFI shadow-clear coverage for `strto*`/`wcsto*` was incomplete and,
  where present, overly conservative** (#839). Only `strtol`/`strtod` had
  an `FFI_SHADOW_BOUNDED` row narrowing the type-shadow clear to their
  `*endptr` output; the other ten members of the family
  (`strtof`/`strtold`/`strtoll`/`strtoul`/`strtoull` and the `wcsto*`
  equivalents) fell through to the default whole-allocation clear. Added
  the missing rows, then added `other_args_readonly` to `FfiShadowRule` (a
  new opt-in field, defaulting to `false` so existing rows are unaffected)
  and set it for the whole family, since none of these calls ever write
  through their `nptr` argument — `ffi_shadow_backstop` now skips the clear
  entirely for `nptr` instead of applying the default whole-object clear.
  Covered by new negative tests (`test_heap_type_ffi_strtoull_bounded_error.c`,
  `test_heap_type_ffi_strtof_bounded_error.c`,
  `test_heap_type_ffi_strtol_nptr_preserved_error.c`); `man/SAFETY.md`'s
  bounded-write description updated to name the whole family and describe
  the read-only-others behavior.
- **`-c=generated` output could emit a call to a function before its
  declaration, and could silently drop a published-but-bodyless function
  prototype** (#956). `cc_serialize_program`'s emit-event branch
  (`src/serialize.c`) printed each macro-generated function's signature and
  body together, in `PublishNode`/`MakeFunction` event order, which has no
  relation to the call graph — a function referencing another generated
  function published later in program order produced an undeclared-
  identifier error in the emitted C. Fixed by scanning each function's body
  for calls to other not-yet-declared generated functions immediately
  before it is emitted and inserting a forward declaration there, handling
  mutual recursion regardless of creation order without hoisting every
  prototype unconditionally (which would break `#ifdef`-guarded generation
  and struct-tag visibility for a type reached via a captured `#include`,
  see #953). Broadened to also match a generated function referenced as a
  bare value (e.g. cast to a function pointer), not just a direct call.
  The same "dropped bodyless prototype" gap in the fallback (non-emit-
  event) prototype pass is fixed too. Covered by
  `tools/comptime_native_smoke.py` case 38.
- A REPL/debugger prerequisite surfaced while implementing #666:
  `cc_repl_compile_new()` never allocated the RETBUF return-buffer pool, so
  any struct/union/vector-returning call in the REPL (or a debugger
  conditional-breakpoint expression, which shares the same compile path)
  crashed with "return buffer pool was not rehydrated". Extracted into
  `alloc_return_buffer_pool()`, now called from both `gen()` and
  `cc_repl_compile_new()`.
- `dump_type_simple()` printed a struct/union/enum's *declarator* name
  instead of its tag (`struct P p; p` printed as `"struct p"` regardless of
  the tag name); now prefers `struct_tag`/`enum_tag`.
- A variably modified type (VLA) at file scope was silently accepted and
  then crashed `sizeof()` on the resulting global; file-scope VLAs are now
  a compile error (C11 6.7.6.2p4/6.9.2p3), covered by
  `tests/test_vla_file_scope.c`.

### Changed

- The bytecode-lib/linkwith test fixtures moved from `examples/` into
  `tests/fixtures/` — they were never user-facing examples, just source
  fixtures for the bytecode linking/static-lib/dynamic-lib test suite.
  Internal only; no user-facing path changes.

## [0.2.4] - 2026-08-09

### Added

- **New `-Wint-conversion` warning (part of `-Wall`) for an implicit
  integer↔pointer conversion with no cast** — `warn_implicit_conversion()`
  (`src/type.c`) had branches for pointer↔pointer, integer↔integer and
  float conversions, but no branch matched an int/pointer pair, so e.g.
  `const char *p = 'a';` compiled silently and only failed later, at
  runtime, if the resulting garbage pointer was dereferenced. Covers
  assignment/scalar initialization, `return`, and prototyped call
  arguments; suppressed for the null pointer constant `0`. Does not cover
  file-scope/global initializers, which take a separate constant-evaluation
  path.

### Fixed

- **A referenced `extern` global variable that is never defined anywhere
  resolved silently instead of erroring, and — more seriously — redeclaring
  the same global (across an `extern` declaration and its definition,
  within one translation unit or across separate ones) could silently read
  the wrong global's value or the wrong constant offset entirely** (#957).
  Global variable references compile to a data-segment offset baked
  straight into the `Obj` at codegen time (`gen_addr`, `src/codegen.c`),
  but every declaration of a global created its own `Obj`
  (`new_gvar`/`global_variable()`, `src/parse.c`) with nothing reconciling
  them — codegen's allocation loop gave each one its own slot, and
  `cc_link_progs` (`src/linker.c`) canonicalized definitions across
  translation units by name but never propagated the resulting offset onto
  the declaration-only `Obj`s it dropped from the merged list. Concretely,
  before this fix: `extern int g; int f(void){return g;} int g=42;` read 0
  instead of 42 from `f()`; `int g=42; extern int g;` likewise read 0; and
  linking two files where one declared `extern int g;` and the other
  defined `int g=42; int pad=7;` silently read `pad`'s value (7) instead of
  `g`'s (42), with the result depending on file order. Fixed by
  canonicalizing every redeclaration of a global variable within a
  translation unit onto a single `Obj` (`merge_global_decl()` in
  `src/parse.c`, keyed by a new per-TU `global_decl_map`) and propagating
  each canonical global's offset onto the alias `Obj`s `cc_link_progs`
  drops (`global_aliases` array, populated in `src/linker.c`, applied in
  `src/codegen.c`'s `gen()` right after the data-segment allocation loop).
  A referenced-but-never-defined global is now a hard `undefined global:
  <name>` compile error, mirroring the existing `undefined function: %s`
  check (suppressed, not deferred, under `-c`/`--link`, since there is no
  name-based data relocation mechanism for globals); `sizeof()` of an
  undefined extern global still compiles, since the reference is only
  counted where codegen actually materializes an address. Two full
  definitions of the same global (`int g=1; int g=2;`) are now also a
  `redefinition of '<name>'` error, matching the pre-existing cross-TU
  check — previously silently accepted, with the second initializer
  winning. `environ` (used by `posix_spawnp` in the POSIX test suite) is
  now exposed via the same host-global accessor macro pattern as `errno`
  and `stdin`/`stdout`/`stderr` (`include/unistd.h`,
  `src/stdlib/posix.c`), since it would otherwise have started hitting the
  new undefined-global error as an inert, host-disconnected guest global.
  See `tests/test_extern_global_undefined.c`,
  `tests/suites/test_suite_global_canonicalization.c`,
  `tests/test_cross_tu_global_offset.c`/`_reversed.c`, and
  `tests/test_global_redefinition.c`.

- **`tests/failures/` was silently excluded from test discovery, so every
  test inside it — 41 files — never ran** — `discover_tests()`
  (`tools/testing/discovery.py`) filtered out any path with a `failures`
  component, but nothing generated or consumed that exclusion elsewhere; it
  quietly turned the directory into dead weight, including real regression
  coverage for tickets #1, #78, #172, #194, #195, #357, #884. The exclusion
  is removed and every file audited individually: 33 tests moved into
  `tests/`/`tests/macros/` (12 already correct as-is; 3 that needed the
  `__builtin_quote` diagnostic fix below; 3 memory-tagging tests that needed
  `CCCC_FLAGS: --memory-tagging` + `EXPECT_RUNTIME_ERROR`, since without the
  flag they were passing by accident on reused-but-unvalidated memory
  content; 14 error-recovery/`_BitInt` tests that were already correctly
  rejected at compile time but had never been marked `EXPECT_COMPILE_ERROR`;
  1 `extern`-symbol test additionally marked `CCCC_C4_SKIP`, matching
  `test_bytecode_link_unresolved.c`'s existing #565 rationale; 1 typedef
  test rewritten from a bare non-42 return to the exit-42 assertion
  protocol). 7 files deleted as invalid C predating the current test
  conventions (a misunderstanding of the declaration-comma vs.
  comma-operator distinction, and `##` at the start of a `__VA_OPT__`
  argument, both rejected by GCC/Clang too) or an incomplete scratch
  fragment with no `main()`. 1 file (an unreferenced `extern` global that
  resolves silently instead of erroring) deleted pending a separate ticket,
  since fixing it is a VM-level design question, not a test fix. Also added
  `tests/test_va_opt_basic.c`, since the audit found `__VA_OPT__` had no
  surviving coverage anywhere in the suite.

- **`error()` (no source location) never printed the "N error(s)
  generated." summary that `error_tok()` produces, and never incremented
  `vm->error_count`** — three `Quote()` validation diagnostics in
  `quote_scan_and_rewrite()`/`quote_substitute()`/`quote_core()`
  (`src/reflection.c`) used the location-less `error()`, so the test
  runner's `has_compile_error` check (`tools/testing/runner.py`, which keys
  off that summary text) couldn't distinguish "correctly rejected at compile
  time" from "compiled fine and the program itself returned a non-42 exit
  code" — the *test harness* misclassified three correct
  `EXPECT_COMPILE_ERROR` tests as failures. Converted to `error_tok()` with
  the offending token, which also gives these diagnostics a real source
  location instead of none.

- **`Quote()`/`QuoteN()` templates no longer drop statements after the
  first one** — an unbraced multi-statement template like
  `Quote("if (!$1) $1 = f($2); return $1;", a, b)` parsed only the leading
  `if` (`quote_core()` in `src/reflection.c` called `cc_parse_stmt()` once
  and never inspected the leftover tokens), silently discarding the
  `return` and leaving the generated function falling through with an
  undefined return value — a silent-miscompile-shaped footgun with no
  warning or error. An unbraced multi-statement template is now
  transparently wrapped in braces and parsed as a block, exactly like the
  already-safe `Quote("{ ... }")` form; any template that still leaves
  tokens unparsed after that (e.g. trailing garbage on an expression
  template) is now a compile error instead of a silent drop. (#955)

- **`RunCustom`'s vendored shell now performs POSIX-correct quote removal,
  backslash escaping, and `$VAR`/`${VAR}` expansion** — the lexer
  (`src/build_shell.c`) previously only recognized a quote as the very
  first character of a word, and even then returned its interior
  unprocessed: an embedded quote (`pre'mid'post`), a backslash escape, or a
  delimiter inside quotes (`a";"b`) all passed through with the quote
  characters still attached instead of being stripped. A `RunCustom`
  command whose value needed embedded quotes — e.g. `cccc -c=generated
  ... -DSOME_MACRO='"literal"'` — therefore handed the child a
  multi-character character-constant instead of a string literal, silently
  converted to a pointer and dereferenced (SIGSEGV in the *child* process,
  not the outer `--build` process, which correctly reported the step's
  failure and non-zero exit). The word reader now performs real quote
  removal and backslash escaping per POSIX, plus `$VAR`/`${VAR}` expansion
  from the process environment as a single non-re-split, non-globbed
  literal chunk. (#954)

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
