# Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- `tools/tests.py --native`: a serializer round-trip test mode, mirroring
  `--c4`'s bytecode round-trip. Compiles each eligible positive test with
  `-c=native`, confirms the compiled artifact exists, then runs it and
  checks its exit code against the VM run — catching the class of bug where
  `-c=native` compiles and runs cleanly but returns a different answer than
  the VM. Opt-in (not wired into CI yet); see man/TESTING.md's "Native
  round-trip mode" section.

### Fixed

- `-c=native`: `include/pthread.h`'s bundled `pthread_mutex_t`/
  `pthread_cond_t` were a VM-only opaque-handle projection (24/16 bytes on
  macOS arm64) that `-I./include` fed straight to the real host
  `pthread_mutex_init()`/etc. — which writes the real struct's full size
  (64/48 bytes), silently corrupting adjacent memory with no compile or
  link error. `include/pthread.h` now hands off to the real host
  `<pthread.h>` (`#ifdef __CCCC__`/`#include_next`, matching `stdio.h`/
  `errno.h`/`fenv.h`), `PTHREAD_MUTEX_INITIALIZER`/`PTHREAD_COND_INITIALIZER`
  now re-emit as the bare host macro instead of CCCC's own designated
  initializer, and `_Thread_local`/`__thread` globals are now emitted with
  their storage class (previously silently dropped). A cccc-only header's
  own nested `#include`s (e.g. `include/threads.h`'s `"pthread.h"`/
  `"time.h"`) are now replayed too, instead of being neither replayed nor
  re-derived.
- `src/serialize.c`: a `float` global initializer whose value prints with
  no decimal point under `%.9g` (e.g. `1.0f`) used to emit the invalid
  token `1f`; now forces a decimal point before the `f` suffix.
- `src/serialize.c`: member access through an anonymous struct/union member
  (e.g. `s.i` where `i` belongs to an unnamed nested struct) used to emit
  the invalid `s./* unknown */.i`; the anonymous link is now left
  transparent, matching plain C semantics.
- `-c=native`: six narrowly-scoped serializer/decl round-trip bugs found by
  `--native`'s corpus sweep, each traced to a single root cause:
  - a user-written `alloca()` call re-emitted as a bare, undeclared
    `alloca(...)` — now emitted as `__builtin_alloca(...)`, which every
    supported native cc supplies with no header.
  - `<fenv.h>`/`<errno.h>` and other CCCC-preprocessor-dependent identifiers
    (`FE_*`, `fenv_t`, `isnan`/`isinf`/`signbit`/`fpclassify`, `FLT_ROUNDS`,
    non-finite float literals) failing to compile — a replayed `#include
    <fenv.h>`/`<errno.h>` in a `-c=native`/`-c=generated` TU resolved back
    to CCCC's own bundled copy (via `-I./include`), still needing macros
    only CCCC's own preprocessor injects. `include/fenv.h`/`include/errno.h`
    now guard their CCCC-flavored content behind `#ifdef __CCCC__`
    (unconditionally defined while CCCC parses guest source) and
    `#include_next` the host's own, self-contained header in the `#else`
    branch — taken only when a real host compiler reprocesses the same
    physical file during serializer replay; `tools/audit_ffi.py`'s
    guard-presence check whitelists `__CCCC__` accordingly
    (`GUEST_ONLY_DECL_GUARDS`). `serialize.c`'s `native_accessor_shims`
    table gained `__cccc_isnan_f/d`, `__cccc_isinf_f/d`,
    `__cccc_signbit_f/d`, `__cccc_fpclassify_f/d`, and `__cccc_flt_rounds`
    (each spelled with a portable `__builtin_*` intrinsic in its body, not
    the guest-facing macro name, since `<math.h>`'s/`<float.h>`'s own
    isnan/FLT_ROUNDS macros dispatch right back to these same shims —
    reading them literally would recurse); `include/math.h`'s/
    `include/float.h`'s own now-redundant `extern` declarations for the
    same five identifiers are likewise guarded on `#ifdef __CCCC__`, since
    an unconditional one would conflict with the shim's `static`
    definition the same way `errno.h`'s did. Non-finite float literals
    (`inf`/`nan`) now emit `__builtin_inf()`/`__builtin_nan("")` instead of
    the invalid bare tokens `inf`/`nan`.
  - an untagged, alias-less struct/union global (e.g. `static const struct
    { ... } codes[]`) forward-declared twice under two structurally
    distinct anonymous types — the #918/#928 forward-declaration passes now
    skip such a global; it has no name to forward-declare in the first
    place, so the real definition supplies the only copy of the type.
  - a `const`-qualified local's declaration and initializer, split across
    two statements by the serializer's local-hoisting pass, re-emitting an
    assignment to a `const` object — the hoisted declarator now strips only
    the top-level `const`.
  - a function returning a function pointer (`int (*f(void))(int, int)`)
    mis-serialized as `int (*)(int, int) f(void)` — `serialize_type_decl`'s
    `TY_FUNC` case and `serialize_function_signature` now build the
    declarator via the same inside-out buffer recursion `TY_ARRAY`/`TY_PTR`
    already use.
  - an `asm("symbol")`-labeled function declaration losing the label
    entirely, so the real symbol was never referenced and the link failed
    — the label is now re-emitted via the portable
    `asm(__CCCC_ASM_PREFIX__ "symbol")` idiom (`__USER_LABEL_PREFIX__`,
    double-stringized), correct on both underscore-prefixed (Darwin) and
    non-prefixed (Linux/ELF) targets; a `static` prefix is also suppressed
    for an asm-labeled declaration, since internal linkage is meaningless
    for a symbol the label aliases externally.
- `src/serialize.c`: a folded `ND_NUM` integer literal (e.g. constant
  folding through a `(double)` cast) serialized as a bare `%lld` of its
  raw bit pattern with no sign or width suffix — an unsigned 64-bit value
  ≥ 2^63 (e.g. `ULLONG_MAX`) printed as the unsuffixed text `-1`, which a
  real host compiler reads back as a negative `int`, changing the result
  of a later implicit conversion; `INT64_MIN` printed as the bare token
  `-9223372036854775808`, not a valid signed literal at all. Both now
  serialize with a sign/width-accurate `U`/`ULL`/`LL` suffix, or as
  `(-9223372036854775807LL - 1)` for `INT64_MIN` (#1031).
- `-c=native`: a plain `cccc foo.c -c=native` with no explicit `--std=`
  used to forward no `-std=` to the host `cc` at all, relying entirely on
  its own default standard — which can be older than CCCC's resolved
  default (`gnu23`) and silently reject a legitimately-emitted C23
  construct. `run_native_backend()` (`src/main.c`) now quietly probes the
  host `cc` down a ladder from CCCC's resolved default toward older
  standards (`gnu23` → `gnu2x` → `gnu17` → `gnu11`) and forwards the
  newest rung actually accepted; an explicit `--std=` is still forwarded
  verbatim, unprobed (#1053).
- `src/main.c`: `run_native_backend()` built each `-std`/`-D`/`-U`/`-l`
  flag it hands the host `cc` into a stack buffer and pushed the buffer's
  address into the argv rather than a copy — the `-std` flag was
  dangling by `exec()` time, and each `-D`/`-U`/`-l` loop's buffer
  reused one stack address across iterations, silently collapsing
  distinct defines (e.g. `-DA=1 -DB=2`) into duplicates. Now heap-backed,
  freed after the spawn. Separately, `parse_define()` used to split a
  `-D` argument's `NAME=VALUE` in place, permanently truncating the same
  string this reuses later — now splits via a bounded copy (#1065).
- The VM's calling convention passed a struct/union by-value function
  parameter as a raw pointer to the caller's own object, with no copy — a
  write through the parameter inside the callee silently mutated the
  caller's argument. Every real host C compiler, and `-c=native`, already
  copied the argument; only the VM's own convention didn't. Fixed by
  copying each struct/union parameter into a fresh frame-local scratch
  slot in the callee's own prologue and rebinding the parameter to point
  at the copy (`gen_function`, `src/codegen_func.c`). This was also the
  actual root cause behind a previously-filed `va_list`-forwarding
  divergence report, whose stated premise had it backwards (#1078).
- A block literal defined inside a genuinely nested function, capturing a
  variable owned by one of that function's own ancestors (not the nested
  function's own local), silently miscompiled -- the variable's address was
  chased through the block's own `__static_link`, which holds a descriptor
  pointer rather than a frame base pointer, reading garbage bytes. Fixed in
  two parts: the block's capture-collection ancestor climb now walks
  through a genuinely nested enclosing function the same way it already
  walked through an enclosing block (`parse_blocks.c`/`parse_decl.c`), and
  the block's descriptor-population codegen gained the matching
  static-link-chase source arm it was missing (`codegen_expr.c`, sharing
  `gen_addr`'s own chase via a new `emit_static_chain_var_addr()` helper in
  `codegen_addr.c`) (#1076).
- `src/preprocess.c`: `add_macro()` stored a `-D` command-line macro's
  caller-freed name pointer directly as `Macro.name` instead of copying
  it — every other caller passes arena/token-backed storage, so this was
  a silent, heap-layout-dependent use-after-free (confirmed via
  AddressSanitizer, reproducible on top of any commit) until an unrelated
  allocation-pattern change happened to make it crash. Now always copies
  via `arena_strndup()` (#1097).
- `-c=native`: a function-local `static` initialized with a
  [GNU] labels-as-values address (`static const void *disptab[] = { &&L0,
  &&L1 };`, the usual dispatch-table idiom for a `goto *disptab[i]`
  interpreter loop) hard-errored ("unresolved relocation target") — real
  GCC/clang both accept this construct, but only at function scope,
  and `-c=native` unconditionally hoisted such a `static` to file scope,
  where a label's address has no C spelling. Such a global's real
  definition is now deferred into its owning function's own body instead
  (`collect_deferred_static_labels()`, `src/serialize.c`), the only place
  the address is legal; a candidate is left undeferred (still hits the
  original diagnostic, rather than emitting broken C) whenever it's read
  from more than one function (a nested function or block literal), or
  whenever another global's own initializer takes its address (e.g.
  `static void **p = tab;` sitting next to `static void *tab[] = {&&L};`
  — deferring `tab` would otherwise leave `p`'s own file-scope reference
  naming a symbol that no longer exists there) (#1044). Verifying this
  fix against a real-world, deeply nested/heavily-shared program
  (`tests/test_minilua.c`) surfaced a stack-overflow/DAG-blowup hazard in
  this fix's own AST walker, now a proper explicit heap-backed stack with
  pointer-identity deduplication instead of recursion.
- `-c=native`: a bodiless declaration (`extern int close(int fd);`) written
  in a primary source file that also `#include`s a CCCC-bundled header
  (e.g. `<fcntl.h>`) was silently dropped from the emitted C — bundled
  `fcntl.h` itself `#include`s bundled `unistd.h`, which is what actually
  declares `close()`, so the bodiless-declaration gate (`#901`) saw the
  declaration's own token pointing at `unistd.h` and assumed the replayed
  `#include <fcntl.h>` already supplied it. True for a real host header
  reached transitively, but not for CCCC's own bundled chain: the replayed
  `#include` resolves to the *host's* `fcntl.h` under `-c=native`, which
  does not declare `close()` — `use of undeclared identifier 'close'`. Not
  caught by the test suite (`tools/testing/native.py` always passes
  `-I./include`, which masks it), but a real gap for a plain
  `cccc -c=native foo.c -o foo` invocation. Fixed via a new
  `cc_file_is_cccc_bundled()` marker (`src/preprocess.c`) distinguishing a
  CCCC-bundled header from a real host one; the `#901` gate now also emits
  a declaration sourced from a bundled-but-unreplayed header, gated on
  `obj->is_used` (#1096).
- `-c=native`: `#1031`'s own fix (a `sizeof`/`_Alignof` of a `from_include`
  type re-materializing textually against the real host layout) only
  covered a bare expression node; three more contexts that fold such a
  constant into a plain integer and discard the node now carry the same
  provenance through instead — an array dimension (local, or an
  uninitialized global only — an initialized global's dimension stays
  folded so it can't disagree with its own byte-image initializer, and
  a struct/union member's dimension stays folded for the same reason a
  bitfield width does, see below), a `case` label, and an enum value
  (propagated to every *use* of the enumerator too, not just its own
  declaration — except a later enumerator that auto-increments from it,
  which stays folded rather than risk disagreeing with a re-materialized
  predecessor). A bitfield width and a global initializer's own byte image
  remain open — not merely deferred, actively unsound to fix the same way,
  since both feed a layout CCCC itself also emits (#1095).

## [0.2.15] - 2026-08-15

### Added

- New `-Wnative-name-collision` warning (part of `-Wall`), naming a
  collision the `-m`/`-c=native`/`-c=generated` serializer's rename
  passes (#1014/#1015/#1016) can't resolve — currently a header-exposed
  `enum`'s enumerator colliding with a plain file-scope identifier
  declared in a `.c` that doesn't include that header (#1017). Neither
  side of that collision can be safely renamed (the replayed `#include`
  binds the enumerator textually; the identifier's rename would either
  change an emitted symbol or widen an existing "only rename dups"
  rule), so the collision still reaches the generated output for the
  host compiler to report — but the warning now points at it first,
  naming the enumerator and the header, instead of leaving the user
  with only the host compiler's own diagnostic against a deleted
  `/tmp` temp file under `-c=native`. Fixing this also surfaced a real
  gap: nothing flushed a warning queued *during* serialization itself,
  so this warning (and any future one raised from `src/serialize.c`)
  was silently dropped even with `-Wall` passed — both
  `cc_serialize_program()` callers now print it.

### Fixed

- **Two translation units each independently completing a same-named but
  differently-shaped `struct`/`union`/`enum` tag collided in
  `-c=native`/`-m` output** (#1014) — the opaque-handle idiom used
  per-backend (a shared header forward-declares the tag, each `.c`
  privately supplies its own definition) produced a host "redefinition"
  error even though the VM ran the program fine. Fixed by renaming every
  colliding group but the header-exposed one to `<name>__cccc_dup<N>`,
  regardless of which `.c` is listed first on the command line.
- **Renaming a colliding `enum` tag (#1014) didn't rename its colliding
  *enumerators*** (#1015) — two enums sharing an enumerator name (tags
  colliding or not) still hit a host "redefinition of enumerator" error
  after the tag itself was renamed apart. Fixed the same way, following
  the identical header-exposed/keeper rules so the two passes never
  disagree about which group keeps the plain spelling.
- **A colliding enumerator wasn't renamed against a plain file-scope
  identifier of the same name** (#1016) — `static int AA;` in one file
  and `enum E { AA };` in another (equally an `extern` global or a
  function) collided, since neither #1014's tag pass nor #1015's
  enumerator pass ever looked at the ordinary-identifier namespace an
  enumerator also shares. Fixed by renaming every colliding enum
  group's copy of the name instead — the file-scope identifier itself
  is never renamed (renaming external linkage would change the emitted
  symbol; renaming a unique `static` would be unsound in general).
- **An opaque-handle struct's definition could be dropped or emitted as
  a bare forward declaration across translation units** (#1010),
  depending on which `.c` completing it was parsed first or last — a
  host "incomplete definition of type" error even though the VM ran the
  program fine. Also fixed a duplicated `static` local's hoisted
  file-scope shadow global being declared twice in `-m`/`-c=native`
  output (#1011, harmless but unintentional).
- **`--uninitialized-detection`/`--safety=max` falsely reported
  `UNINITIALIZED VARIABLE READ` for a scalar local written through its
  address rather than a direct assignment** (#1008), most commonly the
  ordinary C out-parameter idiom (`void fill(int *out){ *out = 42; }`).
  The same gap also covered a `__block` local written from inside a
  block literal and a plain local written from inside a nested
  function. Fixed by exempting a local from the read-side check once
  its address is taken, mirroring the existing struct/array/wide
  `_BitInt`/`_Decimal` exemptions. A downstream false
  `MEMORY LEAK DETECTED` report (#1009) was investigated and found to
  be entirely caused by #1008 — the uninit trap silently aborted the
  program before its own `free`/teardown code ran — and closes as a
  duplicate with no separate fix needed.

## [0.2.14] - 2026-08-14

### Fixed

- **`-c=native`/`-m` dropped or duplicated a `static` function or
  file-scope typedef declared in a non-primary translation unit**
  (#1002, #1006). A multi-file `cccc a.c b.c` build could silently drop
  a `static` function or a `typedef`/`struct`/`enum` written in `b.c`
  ("call to undeclared function" / "unknown type name"), or emit a
  colliding definition for two same-named `static` functions in
  different files ("redefinition"). Root cause: several serializer
  predicates keyed off `vm->compiler.primary_file`, which only ever
  names the *first* input file. Fixed by tracking every command-line
  input file and checking membership in that set instead; colliding
  `static` names across files are now automatically renamed rather than
  emitted twice.
- **`-c=native` emitted `goto (null);` for every `break`/`continue`, and
  `switch` bodies were broken three more ways** (#1005). `break`/
  `continue` printed a literal, invalid `goto (null);` — a host compile
  error for any `-c=native` program using either inside a loop. Fixed to
  emit real `break;`/`continue;` keywords. Investigating the fix
  surfaced that `switch` itself was also broken: only each case's first
  statement was emitted, cases came out in reverse source order with
  `default:` forced last (breaking fallthrough), and GNU case ranges
  (`case 1 ... 5:`) collapsed to their start value. All fixed together.
- **Preprocessor macro/include-guard state and parser scope leaked
  across translation units** (#1001). Every `.c` file on one `cccc`
  command line shared one preprocessor state, so a `#define` in one
  file was silently visible in another with no `#include` at all; a
  matching parser scope leak meant every TU's declarations stayed
  visible from every later TU's own parse, independently masking the
  include-guard half of the same bug. Both are now reset per
  translation unit. One intentional behavior change: a header that
  *defines* (not just declares) a non-`extern` global, `#include`d
  unguarded from more than one TU, is now a `redefinition` error,
  matching what a real linker does with the equivalent two-object-file
  build.
- **`-c=native`/`-m` couldn't serialize a cccc-owned polyfill header**
  (#1003) such as `<stdbit.h>`/`<threads.h>`/`<uchar.h>` — the
  generated C tried to `#include` a header the host toolchain has no
  guaranteed copy of, producing an unresolvable "file not found" even
  though CCCC itself compiled the program fine. Such headers are now
  marked cccc-only at the point they're resolved, so their content is
  re-derived instead of replayed as an `#include`.
- `setenv`/`unsetenv`/`putenv` are now registered in the FFI stdlib —
  `getenv` was registered but its siblings weren't, so any guest
  program calling them died with "undefined function".

### Changed

- Split `src/stdlib/posix.c` (3,722 lines) into 16 per-domain files —
  `posix_io.c`, `posix_net.c`, `posix_poll.c`, `posix_wait.c`,
  `posix_dir.c`, `posix_search.c`, `posix_sched.c`, `posix_statfs.c`,
  `posix_ipc.c`, `posix_wordexp.c`, `posix_aio.c`, `posix_mqueue.c`,
  `posix_ndbm.c`, `posix_spawn.c`, `posix_lang.c`, plus a shared
  `posix_util.c`/`.h` — and mapped 43 of the 52 POSIX headers in
  `tools/stdlib.tsv` to their own domain's registrar directly, instead
  of the single monolithic aggregator. A guest program `#include`ing
  only `<poll.h>`, for example, now lazily registers only that
  domain's handful of FFI wrappers instead of the full ~340-function
  POSIX surface. Pure refactor plus a registration-granularity change —
  no wrapper behavior changed, and headers whose functions genuinely
  span more than one domain (`unistd.h`, `strings.h`,
  `sys/resource.h`) stay on the aggregator.
- Split `src/codegen.c` (9,438 lines) into 7 files by section
  (`codegen_regalloc.c`, `codegen_call.c`, `codegen_emit.c`,
  `codegen_addr.c`, `codegen_expr.c`, `codegen_stmt.c`,
  `codegen_func.c`) and `src/parse.c` (16,023 lines) into 10 files by
  section (`parse_core.c`, `parse_types.c`, `parse_checked.c`,
  `parse_init.c`, `parse_stmt.c`, `parse_analysis.c`, `parse_expr.c`,
  `parse_blocks.c`, `parse_postfix.c`, `parse_decl.c`). Purely
  mechanical — no behavioral changes.

## [0.2.13] - 2026-08-14

### Fixed

- **Frontend crash and `-c=native` miscompiles on a forward-referenced
  static-function vtable pattern** (#999). `cccc -t`/`-c=native` on a
  `static const` struct of function pointers to file-static functions
  declared before their definitions (a common collector/dispatch-table
  idiom) crashed with SIGSEGV and no diagnostic output at all. Root cause:
  a member-access expression that already failed to typecheck left a
  placeholder AST node with a NULL left-hand side, which a compound-
  assignment lowering path then dereferenced unconditionally instead of
  propagating the error. Fixed to surface the real diagnostic instead.
- **`-m`/`-c=native` serializer bugs on the same vtable pattern** (#999,
  same ticket). A global initializer taking a later-declared function's
  address reached the output ahead of that function's own prototype
  ("use of undeclared identifier"); a multi-file build's `-m`/
  `-c=generated` output could report a spurious "unresolved relocation
  target" for a symbol merged from an earlier translation unit; a `static`
  function defined in a header shared by more than one translation unit
  was re-emitted once per TU ("redefinition"); and a scalar typedef
  (`typedef unsigned long DyValue;`) lost its name in serialized
  signatures, printing its canonical underlying type instead — harmless on
  most platforms, but a hard "conflicting types" error where the typedef
  and its canonical spelling denote genuinely different types (e.g.
  `uint64_t` vs. `unsigned long` on LP64 Darwin). All four fixed.
- **A translation unit with no global definitions was reported as a parse
  failure** (#999, found while investigating the above). A `.c` file
  holding only typedefs/prototypes is legitimately empty of new globals,
  but was treated as an unconditional failure with the process exit code
  left at 0 regardless — silent success dressed up as a bogus error.
- **A `static` function defined in a *non-first* command-line input file
  was silently dropped from `-m`/`-c=native` output** (found investigating
  #1002). The "was this supplied by a replayed header" check compared
  against the *primary* input file only, so the identical shape in a
  second or later input file was misidentified as header-supplied.
- **Two different `.c` inputs each defining a same-named `static` function
  or global collided in `-c=native` output** (#1002). Internal-linkage
  symbols are deliberately left uncanonicalized across translation units,
  so two inputs sharing no header but defining, say, `static int
  helper(void)`, both reached the merged output unrenamed. Now renamed
  automatically on collision; a name with no collision is untouched.
- **A cccc-owned polyfill header (`stdbit.h`, `stdckdint.h`, `threads.h`,
  `uchar.h`, `Availability.h`, `decimal_math.h`) was replayed as an
  unresolvable `#include` under `-c=native`** (#1003). These headers are
  the only implementation likely to exist on a typical host at all, unlike
  an ordinary standard header the host is expected to have; replaying them
  verbatim produced a `file not found` from the downstream compiler even
  though CCCC itself compiled the program fine. Such a header's `#include`
  is now suppressed and its content re-derived into the output instead;
  `decimal_math.h`'s VM-only FFI symbols have no host definition to
  re-derive against, so it is a hard compile error instead.
- **Preprocessor macro definitions and `#pragma once`/include-guard state
  leaked across translation units** (#1001). Every `.c` file passed on one
  `cccc` command line shared a single preprocessor state, so a `#define`
  in one file was silently visible in another with no `#include` at all,
  and a header's include guard, once tripped by the first file to include
  it, emptied that same `#include` for every later file. A second,
  load-bearing bug in the parser's scope handling had masked the
  include-guard half of this for a common case, so both were fixed
  together. Each `.c` input is now its own translation unit for
  preprocessing purposes, as the standard requires.

## [0.2.12] - 2026-08-14

### Fixed

- **Duplicate `struct` definition in `-c=generated` when a standard header
  resolves from the embedded table** (#998). A TU that both
  `#include @comptime`-routes and plainly `#include`s the same standard
  header (e.g. `<time.h>`, so a comptime macro can `GetType()` it while
  ordinary code also uses it) emitted both a re-derived definition and the
  replayed `#include` line — a redefinition a real system compiler rejects.
  Reproduces only when the header resolves from the embedded `src/std.c`
  table rather than on disk (any CWD other than the repo root, or without
  `-I`); the ticket's own hypothesis about the `@comptime`/plain collision
  itself was not the cause. The `PP_INCLUDE` handler's embedded-header
  branch never registered its synthetic `<embedded>/<name>` path into the
  map `-c=generated`'s duplicate-definition filter (#953) consults — now
  fixed.
- **Block capture of a function-local struct/union/enum type** (#989). A
  block literal capturing a variable whose type was declared inside the
  enclosing function hard-errored, since the lifted env struct is emitted
  at file scope, ahead of the function that brings the tag into scope.
  Fixed by hoisting the type to file scope, including the tagless-local-
  aggregate case that silently emitted broken, duplicated struct bodies
  even before this fix.
- **`Block_release`'s fallback `free()` + block preamble ordering** (#990,
  #993). `Block_release(b)` falling back to a synthesized `free` prototype
  (no user `free` in scope) generated a call to an undeclared `free` under
  `-c=native`; a by-value capture of a header-declared type (e.g.
  `struct tm`) could be serialized before that header's own definition was
  in scope. Also fixed: a TU that uses a block *type* but declares no block
  *literal* dropped the block preamble (env struct, copy/release helpers)
  entirely.
- **Block capture of a by-value aggregate larger than 8 bytes** (#994). The
  block descriptor gave every capture a flat 8-byte slot regardless of
  type, silently truncating a wider struct/union/array capture and storing
  a dangling frame pointer for a wide `_BitInt`/`_Decimal128` capture
  instead of a snapshot. Fixed with a size-aware descriptor layout;
  also fixes a by-value struct/vector *parameter* capture (itself passed
  by pointer) and a bare array capture (accepted by this compiler, invalid
  C for the serializer's old comma-assignment form).
- **Locals in a macro-generated function body get frame offset 0** (#996).
  `MakeFunction()` + `FunctionSetBody(fn, Quote(...))` without wrapping in
  `WithFn(fn)` never attached the quoted body's locals to `fn`, so every
  local defaulted to offset 0 and aliased the same frame slot — SIGBUS or
  silent corruption, not block-specific despite the ticket's repro using a
  block literal. Fixed by adopting `vm->compiler.locals` onto `fn` when no
  other function currently owns it.
- **Lifted block function dropped from `-c=generated` output** (#995). A
  block literal parsed while building a macro-generated function's body
  (the #996 pattern) executed correctly in the VM but was silently missing
  from `-c=generated` output — the generated C failed to link. Fixed by
  propagating `is_macro_generated` onto a lifted block function.
- **Locals stranded when a nested comptime macro builds another function's
  body** (#997, companion to #996). A comptime macro invoked from inside an
  ordinary function that itself calls `FunctionSetBody` on a *different*
  function without `WithFn` misattaches that function's locals to the
  caller's frame. Detected (not silently auto-adopted, to avoid a partial,
  actively-aliasing move) with a hard compile error naming the fix.

## [0.2.11] - 2026-08-13

### Fixed

- **Serializer `default:` arms now hard-error on an unhandled `NodeKind`/
  `TypeKind` instead of emitting a comment** (#963c, closes #963).
  `serialize_expr`'s and `serialize_type`'s `default:` arms used to emit
  `/* unsupported expr kind N */`/`/* unknown type */` and keep going --
  harmless in expression position (the host compiler then rejects the
  comment loudly), but `serialize_stmt`'s own `default:` routes every
  expression-kind `NodeKind` through `serialize_expr` and appends `;`,
  turning that comment into a syntactically valid null statement: the
  construct silently vanished from the native binary while the VM still
  ran it correctly. This was the root cause behind #925, #926, #927, #930,
  #897, #906, and #952, all discovered independently within a single week.
  Gated on #964 (VLAs, `__builtin_*_overflow`) and #965 (blocks) landing
  first, since flipping the arm earlier would have turned "emits broken C"
  into "cccc internal error" for constructs with no serializer case at all
  yet -- both landed, closing the arc (#963a audit, #963b fixes, #963c this
  guardrail). Both `default:` arms now name the unhandled kind via
  `cc_node_kind_name`/a new `cc_type_kind_name` export. `ND_MACRO_CALL`/
  `ND_INIT_SPLICE` (comptime-internal, consumed before serialization ever
  runs) and `TY_ERROR`/`TY_AUTO` (internal type sentinels) get explicit
  cases instead of falling through the generic default; `ND_BLOCK_LITERAL`'s
  own internal fallback, which emitted the identical marker string from
  inside a handled case, is now a hard error too. `serialize_stmt`'s
  `default:` is deliberately unchanged -- it's the legitimate expression-
  statement route, not an unhandled-kind fallback, and now inherits the
  hard error transitively through `serialize_expr`. Diagnostic-only: no
  opcode/VM change, `CCCC_VERSION` unchanged, no `.c4`/`.c4a` regeneration.

### Added

- **alloca/VLA storage is now reclaimed at block and frame exit** (#981).
  The VM heap is a pure bump allocator -- `free` never moves the bump
  pointer back -- so a VLA declared inside a loop or a recursive function
  previously grew the heap without bound until the process exited. Now
  reclaimed via a LIFO rewind, but only when doing so is provably safe:
  requires every address-keyed safety feature to be off (bounds checks,
  UAF/dangling-pointer/type checks, memory tagging, uninitialized-read
  detection, leak detection, heap canaries -- true at `-0`, false at
  `-1`/`-2`/`-3`) and single-threaded (the VM heap is one global arena, not
  per-thread, so reclamation is simply not attempted once any thread has
  been created -- a documented residual, tracked as a follow-up). A VLA's
  own storage is reclaimed at both the exit of the block that declared it
  (matching its actual C11 6.8.3 lifetime) and its enclosing frame's exit;
  a bare `__builtin_alloca` call's storage -- whose lifetime extends to the
  whole function, not just the block it was called in -- is reclaimed only
  at frame exit, which required splitting `AllocKind` into a third value
  (`ALLOC_KIND_ALLOCA`, distinct from a VLA's `ALLOC_KIND_FRAME`) so a
  block-exit reclaim can never sweep a still-live `alloca`'d block in the
  same lexical block. A `__block` box (`ALLOC_KIND_BLOCK_BOX`) is never
  swept at all, matching `Block_copy`'s existing guarantee that it may
  outlive its declaring frame. Two new opcodes, `HMRK`/`HREL` (block-exit
  watermark push/release) and a repurposed `ALCV` (a VLA's own storage;
  `ALCA` now means bare `alloca` alone), are appended at the end of the
  opcode table, so `CCCC_VERSION` is unchanged and no `.c4`/`.c4a` needs
  regenerating. See [man/SAFETY.md](man/SAFETY.md#vm-heap-allocator) and
  [man/VM.md](man/VM.md#heap-reclamation-981) for the full gating and
  mechanism writeup.

## [0.2.10] - 2026-08-12

### Fixed

- **`CHKD` extended to the atomic ops** (#985). #983 deliberately deferred
  emitting `CHKD` ahead of `ALDR`/`ASTR`/`AXCHG`/`ACAS`, since those
  opcodes' operand words already carry the #497 register-aliasing hazard
  and a naive addition risked reopening it. That premise turned out to
  already be discharged elsewhere in the tree: `ND_CAS`'s existing `CHKNT`
  emission already staged a check between `REG_A0`-`A2` and the `ACAS`
  call, proving it's safe to insert instructions there, and a standalone
  `CHKD` never touches AXCHG/ACAS's own packed-immediate operand word, so
  it cannot reopen #497 either way. Adds one `CHKD` at each of `ALDR`'s,
  `ASTR`'s, and `AXCHG`'s emission sites, and two at `ACAS`'s (object
  pointer and `expected` pointer, since a failed `compare_exchange` reads
  *and* writes through `expected`). No new opcode, so `CCCC_VERSION` is
  unchanged. Covered by `tests/test_atomic_one_past_end_load_error.c`,
  `_store_error.c`, `_exchange_error.c`, `_cas_error.c`,
  `_cas_expected_error.c` (the load-bearing proof of the second `ACAS`
  check), and `_ok.c` (a positive control on the last valid element).

### Fixed

- **`CHKB` rejected forming a legal one-past-the-end pointer** (#983, found
  while fixing #982). `int *e = p + 4;` on a `malloc(4 * sizeof(int))`
  block is legal C to *form* -- only dereferencing it is undefined -- but
  `chkb_common`'s `eff >= header->size` bounds test (`src/ops.c`) rejected
  forming it at all. Couldn't simply relax the comparison to `>`, since
  `CHKB`/`CHKBN` used to be the *only* runtime check on a subscript at all
  (`a[i]` desugars to `*(a+i)`), so that alone would have silently stopped
  a genuine out-of-bounds access (`a[size]`) from being caught. Fixed by
  splitting the check into two: `chkb_common` (`CHKB`/`CHKBN`) now allows
  `eff == size` at pointer *formation*, and a new `CHKD` opcode runs at
  every *dereference* site instead -- scalar loads/stores
  (`emit_load_safety_checks`/`emit_store_ex`), struct/union/wide-`_BitInt`/
  `_Decimal` assignment (the `MCPY` lowering in `ND_ASSIGN`), and vector
  loads/stores (`VLDR`/`VSTR` in `gen_vector_expr`) -- and traps on the
  cases `CHKB`/`CHKBN` no longer do. The split relies on
  `heap_alloc_for_ptr`'s existing inclusive upper bound (`off > h->size`,
  not `off >= h->size`) to resolve an exactly-one-past dereference address
  back to the right allocation. `CHKD` is appended at the end of `OPS_X`
  (`src/cccc.h`), so `CCCC_VERSION` is unchanged and no `.c4`/`.c4a` needs
  regenerating. Not instrumented on the atomic ops (`ALDR`/`ASTR`/`AXCHG`/
  `ACAS`) -- a documented residual, tracked as a follow-up ticket, since
  their operand words already carry the #497 register-aliasing hazard.
  Covered by `tests/test_ptr_one_past_end_form.c`,
  `test_ptr_one_past_end_form_max.c`, `test_ptr_one_past_end_deref_error.c`,
  `test_ptr_one_past_end_deref_read_error.c`,
  `test_ptr_one_past_end_sub_deref_error.c`, and
  `test_ptr_one_past_end_struct_deref_error.c` -- the last four are the
  load-bearing regression tests, proving a genuine out-of-bounds
  dereference is still caught after the formation-side relaxation.

## [0.2.9] - 2026-08-12

### Fixed

- **`&a` on a fixed-size array decayed to `T *` instead of the standard
  `T (*)[N]`** (#975, follow-up to #973). `&a + 1` strided one element
  instead of the whole array. `add_type()`'s `ND_ADDR` case (`src/type.c`)
  special-cased `TY_ARRAY` -- chibicc legacy, non-standard -- decaying to
  `pointer_to(ty->base)` instead of the standard `pointer_to(ty)`, the same
  shape #973 already established for a VLA's `&v`. Fixed by removing the
  special case; array-to-pointer decay for a *bare* array name used as a
  value is a separate mechanism (`ND_VAR` in `src/codegen.c`) and is
  unaffected -- only `&a`'s own static type changed, never the value it
  produces. Covered by `tests/test_addr_of_array_type.c`.

- **Pointer-to-VLA-row subtraction produced garbage** (#976, follow-up to
  #973). `&v[1] - &v[0]` on a 2-D VLA (`int v[n][m]`) did not return `1`.
  Two compounding bugs: `new_sub()`'s "VLA - num" arm (`src/parse.c`) fired
  unconditionally whenever the left operand was VLA-row-pointer-typed, with
  no check that the right operand wasn't itself a pointer -- so a genuine
  ptr-ptr subtraction never reached the dedicated ptr-ptr arm at all,
  instead treating the right-hand pointer as a raw element count and
  multiplying two pointer-ish values together. Once routed to the correct
  arm, that arm itself divided by `TY_VLA`'s placeholder pointer-sized
  `size` (8) instead of the row's runtime `vla_size`. Fixed by excluding a
  pointer right operand from the "VLA - num" arm, and dividing by
  `vla_size` (cast to a signed type -- `vla_size` is an unsigned `Obj`, and
  an uncast division would promote the whole expression to unsigned,
  corrupting a negative result such as `&v[0] - &v[1] == -1`) when the ptr-
  ptr arm's left operand base is `TY_VLA`. Covered by new cases in
  `tests/suites/test_suite_vla.c` and `tools/comptime_native_smoke.py`
  case 51.

- **Multi-dimensional VLA brace initializer silently dropped every
  element** (#977, follow-up to #973). `int v[n][m] = {{1,2},{3,4}}` left
  every element 0. `create_lvar_init` (`src/parse.c`) had no `TY_VLA` case,
  so a nested row's brace group -- itself `TY_VLA`-typed, not a
  scalar/aggregate type the function already handled -- fell through to a
  generic no-op check. Fixed by adding a `TY_VLA` branch mirroring the
  existing `TY_ARRAY` one; `create_vla_init` now delegates to it. Also
  fixed a latent out-of-bounds read found along the way: the initializer's
  element-count scan looked for a `NULL` terminator that `array_initializer1`/
  `array_initializer2`'s VLA branches never actually allocated (both now
  allocate one extra, zeroed slot). VLA brace initialization of any
  dimension is a deliberate CCCC extension -- GCC/clang both reject
  `int v[n] = {...}` outright -- now documented in
  [COVERAGE.md](man/COVERAGE.md); its long-term status is tracked as an
  open design question in #978. Covered by new cases in
  `tests/suites/test_suite_vla.c` and `tools/comptime_native_smoke.py`
  case 51.

- **`&v` on a VLA local returned the wrong address, and whole-row VLA
  assignment silently compiled** (#973, #974). `int (*p)[n] = &v;` read
  garbage through `p`, because `gen_addr`'s `ND_VAR` case returned the VLA
  local's frame slot (which holds the alloca'd data pointer) instead of the
  pointer's own value. `&v`'s *type* was already correct
  (`pointer_to(vla)` **is** the standard `int (*)[n]`, matching real
  gcc/clang) -- decaying it the way `TY_ARRAY` does would have regressed
  `&v + 1`'s stride from a whole row to one element. Fixed by
  special-casing `TY_VLA` in `gen_expr`'s `ND_ADDR` case (`src/codegen.c`)
  to route through `gen_expr` instead of `gen_addr`, covering all three of
  `gen_addr`'s slot-address branches (plain local, block capture,
  outer-function static chain) at once. Separately, `v[1] = w[2];` (a
  whole-row assignment where each side is itself a VLA row in a
  multi-dimensional VLA) compiled instead of erroring "not an lvalue" the
  way the equivalent fixed-size-array assignment already does -- fixed by
  adding a `TY_VLA` arm to `add_type`'s `ND_ASSIGN` check
  (`src/type.c`), excluding `ND_VLA_PTR` (every VLA declaration lowers to
  `ND_ASSIGN(new_vla_ptr(var), alloca(...))`, which must keep compiling).
  Fixing #973 also exposed a native-serializer gap for a pointer-to-VLA
  local's declarator (`int (*p)[n]`, which like a VLA's own declaration
  can't be hoisted ahead of `n`) -- fixed via
  `Obj.deferred_vla_ptr_init` (`src/cccc.h`), skipping just those locals in
  the hoist loop and emitting their real declaration in place. Covered by
  `tests/suites/test_suite_vla.c`'s `test_vla_addr_*`/`test_vla_2d_addr_*`
  cases, `tests/test_vla_row_assign_error.c`, and
  `tools/comptime_native_smoke.py` case 50.

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
