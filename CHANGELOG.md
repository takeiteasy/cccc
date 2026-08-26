#Changelog

All notable changes to CCCC are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.3.14] - 2026-08-26

### Fixed

- `CCCC_NATIVE_CC` used to select both `-c=native`'s guest host compiler AND
  the compiler `--build` uses to compile/link cccc's own object files, via a
  shared `cccc_find_native_cc()` resolver. Setting it to exercise `-c=native`
  under a different compiler family (e.g. Homebrew gcc-16 on macOS) silently
  retargeted the bootstrap build too, which then failed to link with 53
  duplicate symbol errors (`___toupper_l` etc., every `posix_*.o`). Split
  into a separate `CCCC_BUILD_CC` env var (`cccc_find_build_cc()`,
  `src/vm.c`) that `effective_cc_for_target()` (`src/build.c`) now falls
  through to; `CCCC_NATIVE_AR`/`CCCC_NATIVE_LD` renamed to
  `CCCC_BUILD_AR`/`CCCC_BUILD_LD` to match (already build-mode-only). The
  `--build-cache` Level 1 mtime fast path's per-target stamp
  (`<objdir>/.cccc-arch`, #730) is now `<objdir>/.cccc-toolchain` and folds
  in the resolved compiler path alongside the host-arch tag, so a build dir
  reused across compiler families invalidates the same way it already did
  across architectures. The gcc-on-macOS duplicate-symbol link failure
  itself (reachable via an explicit `--build-cc=`/`CCCC_BUILD_CC=` pointed at
  gcc) turned out to be a separate root cause, fixed below (#1199).
- The gcc-on-macOS duplicate-symbol link failure above traced back to
  `src/internal.h`'s `#ifndef __attribute__` / `#define __attribute__(x)`
  guard, which was vacuous — `__attribute__` is a keyword under the GNU
  family, never a predefined macro, so the strip fired unconditionally on
  every compiler. Under a real gcc that deleted the `__gnu_inline__` out of
  Darwin's `<_ctype.h>` `__header_inline` (`extern __inline
  __attribute__((__gnu_inline__))`), leaving a bare `extern __inline` — a
  C99 external definition under `-std=c23` — so every translation unit
  reaching `<ctype.h>`/`<wctype.h>` emitted a full set of ctype/wctype
  helpers, and `--build`'s own bootstrap link failed with the 53 duplicate
  symbols (`___toupper_l` etc.). Clang was never affected, since its
  `__header_inline` resolves to plain `inline` regardless (#1199). Fixed by
  guarding the strip on `!defined(__GNUC__) && !defined(__clang__)` instead
  — every real use of `__attribute__` left in cccc's own host-compiled
  sources was audited first and is diagnostics-only (`format`, `unused`,
  `__no_sanitize_address__`), so this doesn't change codegen anywhere.
  `CCCC_BUILD_CC=<real gcc> ./cccc --build build.c --build-target=cccc` now
  builds and links cleanly on macOS. New `tools/host_attribute_link_smoke.py`
  regression test (wired into `run_tests.py` as `host_attribute_link_smoke`)
  — reproduces on macOS only; glibc's ctype/wctype functions aren't
  extern-inline the same way, so it runs as a harmless sanity check on
  Linux rather than a #1199 regression guard there.
- `tools/comptime_native_smoke.py`'s own skip table
  (`SMOKE_CASE_SKIPS_GCC_MACOS`, #1196) had a `CCCC_AUDIT_NATIVE_SKIPS=1`
  bypass but no staleness *verdict* — a stale entry surfaced as the whole
  script passing, a still-valid one as it failing, inverted from what
  `--native-audit-skips` reports for the filename-keyed tables. Added
  `comptime_native_smoke.py --audit-skips` (`smoke_entry_applies_here()`,
  `tools/testing/__init__.py`), which runs only the cases an entry actually
  governs on this platform+compiler-family and reports STALE/KEPT the same
  way; wired into `run_tests.py` as a new `[ smoke_skip_audit ]` sub-suite
  right after `comptime_native_smoke`. `SMOKE_CASE_SKIPS_GCC_MACOS` only
  applies on macOS+gcc, so on sr.ht (Linux gcc) and GitHub's macOS (clang)
  runners this sub-suite reports "nothing to audit" — the coverage it
  delivers is on a local macOS+gcc-16 run.

## [0.3.13] - 2026-08-26

### Fixed

- `include/stdatomic.h`'s `atomic_fetch_add/sub/or/xor/and` expanded to a
  plain, non-atomic load-then-store — correct only under the VM's GIL
  (never released between bytecode instructions), but a genuine data race
  with silently lost updates once `-c=native` gives real thread
  parallelism (confirmed via stress testing: 7-13% failure rate). Fixed by
  rewriting the macros as a CAS retry loop, reusing the same shape
  `_Atomic x += y` already builds. `atomic_flag_clear`/`_explicit` had the
  same problem more severely — a plain, untagged store that was never
  atomic on either backend — and is now routed through the same tagged
  atomic store `atomic_flag_test_and_set` already uses (#1184).

- `include/stdatomic.h`'s `atomic_thread_fence`/`atomic_signal_fence`
  expanded to nothing — harmless under the VM's GIL, but a real reordering
  hazard under `-c=native`'s genuine parallelism, since publish/subscribe
  code relying on a fence for ordering had no guarantee at all. Fixed with
  a new `ND_FENCE` node lowering to a real `__atomic_thread_fence`/
  `__atomic_signal_fence` under `-c=native`, honouring the `order` argument
  (constant orders serialize to their symbolic `__ATOMIC_*` name, a
  non-constant order passes through verbatim). The VM still emits no
  opcode: the GIL already makes every guest memory access sequentially
  ordered. Also fixed `ATOMIC_FLAG_INIT`, previously spelled as a
  function-like macro (`#define ATOMIC_FLAG_INIT(x) (x)`), non-conforming
  per C11 7.17.1 — now a plain object-like `0` (#1188).

- `include/stdatomic.h` was missing `ATOMIC_VAR_INIT` entirely — a valid
  C11/C17 program using it (`atomic_int x = ATOMIC_VAR_INIT(5);`, C11
  7.17.2p1) got an undefined-macro error rather than a working expansion.
  Fixed with a `(value)` expansion gated to `__STDC_VERSION__ <= 201710L`,
  matching how glibc's and clang's own `<stdatomic.h>` gate it — left
  undefined under cccc's default C23, matching a real C23 compiler, since
  the macro is deprecated in C17 and removed entirely in C23 (#1190).

- `-c=native`: `include/fts.h`/`dirent.h`/`ndbm.h`'s opaque handle types
  (`typedef struct __cccc_FTS FTS;`, and the same shape for `DIR`/`DBM`)
  spelled by their never-completed tag at the declaration site
  (`struct __cccc_FTS *fts;`) but by their alias at a cast site
  (`(FTS *)fts_open(...)`) — the two disagree once the replayed
  `#include` resolves to the real host header, "assignment to
  'struct __cccc_FTS *' from incompatible pointer type 'FTS *'" on a host
  compiler that promotes `-Wincompatible-pointer-types` to an error
  (confirmed: GCC 14+; not clang, and not the older GCC in local
  verification, which is why sr.ht's own build hardware caught this first).
  Fixed in `serialize_type.c`'s `TY_STRUCT` case: an opaque, never-completed
  tag with a `from_include` typedef alias now spells by the alias
  everywhere, so declaration, argument, and assignment sites all agree and
  bind to the host's real type (#1186).
- `-c=native`: a genuinely anonymous struct/union type reused at more than
  one emission site within a function (a compiler-synthesized temp's own
  declaration, and a cast targeting the same type — the shape `++`/`--`/
  `op=` desugaring through a union member routinely introduces) was
  re-derived as a fresh, independent `struct { ... }` body at each site —
  two textually-identical but structurally distinct C types, rejected as
  `-Wincompatible-pointer-types` by the same class of host compiler.
  Reproduced by `test_minilua.c` (a real Lua interpreter). Fixed by
  `hoist_compiler_temp_anon_types()` (`serialize_program.c`), reusing #989's
  `hoist_local_type_to_file_scope()` (previously only applied to block-
  literal captures) for every such compiler temp, so it gets one stable,
  named file-scope definition instead (#1186).
- Two bundled headers (`include/fcntl.h`, plus 22 more) and the generated
  `--testing=native` test-harness `main()` used constructs illegal under
  strict ISO C90 (`//` line comments; a `for (int i = ...)` C99
  declaration) — the same class of bug 0.3.12 fixed for `include/stdlib.h`,
  just not swept exhaustively at the time. Converted the comments to
  `/* */` and hoisted the loop variable (#1186).
- `tools/run_tests.py -jN` could wedge indefinitely: every per-test
  subprocess spawn inherited the harness's own stdin (a child that reads it
  blocks forever) and had no defense against a grandchild (the host `cc`/
  `ld` under `-c=native`, or the compiled test artifact itself) holding the
  inherited stdout/stderr pipes open after its direct parent exited —
  `communicate()` then waits forever for an EOF that never arrives, and
  `subprocess.run`'s own timeout-kill path only signals the direct child,
  never the grandchild holding the pipe. Reported as all worker threads
  plus main parked on futexes with several exited `[cccc]` processes never
  reaped, inside a Rosetta-emulated container at `-j8` (#1185). Not
  root-caused to that specific run, but the mechanism is real and
  reproducible in isolation: fixed by routing every per-test subprocess
  spawn through one chokepoint (`tools/testing/proc.py`'s `run_capture()`)
  that closes stdin, runs the child in its own process group
  (`start_new_session=True`), and kills the *whole group* — not just the
  direct child — on timeout or on a still-unresponsive drain afterward.
  `run_tests.py --process-timeout` now defaults to 600s (was unbounded)
  so a wedge is structurally bounded rather than merely harder to trigger;
  `tools/tests.py` keeps no default for interactive use (#1185).
- `-c=native`: a non-local vector global's own natural alignment wasn't
  stated explicitly when it exceeded 8 bytes — gcc on Darwin/arm64 does not
  derive it from `__attribute__((vector_size(N)))` alone for a global the
  way clang does. Fixed in `serialize_alignas_if_needed` (#1191).
- `tests/test_suite_varargs.c`'s `sum_all_types` call passed an `int` where
  its own `va_arg(ap, long)` read a `long` back — genuine UB that clang
  tolerated and gcc did not, since Apple's arm64 variadic ABI packs slots to
  their argument's own natural size. Fixed the test (#1192).
- `NATIVE_SKIP_TESTS_GCC` (the gcc/Darwin constructor/destructor-priority
  WONT_FIX group) had no platform axis, so it wrongly suppressed those same
  five tests on Linux gcc too, where they actually pass — caught by sr.ht's
  own Linux build hardware reporting all seven as `STALE`. Split into a new
  `NATIVE_SKIP_TESTS_GCC_MACOS` requiring both the platform and
  compiler-family axis to match. `--native-audit-skips`'s own `STALE`
  section is now extracted by its header rather than a blind stdout tail,
  which had been silently missing it (#1193).
- A quoted `#include` inside a bundled header (`sys/stat.h`'s own
  `"../time.h"`) resolved to a literal table-key lookup instead of
  lexically against its own virtual `<embedded>/...` path. Added a real
  embedded-relative include resolver (`src/preprocess.c`), wired into the
  `PP_INCLUDE` handler, `eval_has_include`, and
  `resolve_comptime_include_path` (#1194).
- `-c=native`'s C23 IEC 60559 interchange/classification family
  (`fromfp`/`ufromfp`/`fromfpx`/`ufromfpx`/...) read the wrong ABI return
  register under glibc 2.43 — that release changed the family's *default*
  symbol version to the C23-standardized signature (returns `double`
  instead of `intmax_t`/`uintmax_t`; both `fromfp@GLIBC_2.25` and
  `fromfp@@GLIBC_2.43` coexist), while CCCC's bundled `math.h` still
  declared the old ABI. Fixed with a self-contained shim
  (`serialize_c23_fromfp_shims`, porting the VM's own `cccc_fromfp_impl`
  into the generated C) that never calls the host's own `fromfp*` at all,
  immune to the glibc version split entirely (#1195).
- `_Decimal32/64/128` under `-c=native`: gcc genuinely supports the GNU
  decimal extension (verified on both macOS gcc-16 and Linux gcc 15.2), so
  an earlier plan to hard-error unconditionally would have regressed a
  working configuration. Instead the serializer now tracks whether any
  reachable type uses `_Decimal` and emits a guarded `#error` preamble
  (`#if !defined(__DEC64_MAX__)`), deferring to whichever host compiler
  actually reads the output — gcc unaffected, clang gets a cccc-branded
  diagnostic instead of its own generic one (#1113).
- `tools/comptime_native_smoke.py` had no skip mechanism of its own, unlike
  the main `--native` corpus — its case 114
  (`case_ctor_dtor_native_round_trip`, `__attribute__((constructor(150)))`)
  hit the identical, already-quarantined gcc-on-Darwin constructor-priority
  WONT_FIX gap and hard-failed the whole script under
  `CCCC_NATIVE_CC=gcc-16` on macOS. Added `SMOKE_CASE_SKIPS_GCC_MACOS`/
  `smoke_case_skip_reason()` (`tools/testing/__init__.py`), sharing
  `NATIVE_SKIP_TESTS_GCC_MACOS`'s platform+family two-axis shape and
  `CCCC_AUDIT_NATIVE_SKIPS=1` bypass; a skipped case is now a distinct
  third result state so it can never silently read as a pass (#1196).

### Changed

- The `native`/`native_skip_audit` sub-suites (#1157/#1182), downgraded to
  advisory by 0.3.12 after sr.ht's Linux hardware hit failures/false
  positives neither macOS nor the verification container reproduced, are
  hard-blocking again. Root cause: compiler *family* (clang vs. gcc), not
  GCC version as first suspected — the verification container's own gcc
  simply predates GCC 14's `-Wincompatible-pointer-types` promotion, so it
  never exercised the gcc half of the split. `NATIVE_SKIP_TESTS_CLANG`/
  `NATIVE_SKIP_TESTS_GCC` (`tools/testing/__init__.py`) join the existing
  `_MACOS`/`_LINUX` tables for the divergences that are genuinely
  compiler-family-specific rather than bugs, keyed by a new
  `detect_native_cc_family()` (`tools/testing/platform.py`); the skip-audit
  classifier (`_print_native_skip_audit`, `tools/testing/cli.py`) gained a
  fourth "off_axis" bucket so a platform/family-scoped entry that correctly
  passes off its own axis (e.g. a macOS-only entry audited on Linux) is no
  longer misreported as `STALE` — the root cause of #1182's own six
  false-positive findings. See man/TESTING.md's "Native round-trip mode"
  section (#1186).

### Known issues resolved

- The `native`/`native_skip_audit` sub-suites' return to hard-blocking
  above is now confirmed on sr.ht's own Linux build hardware itself (not
  just the local verification container) across two consecutive green
  pushes (sr.ht builds #1872639/#1872717) — closing the watch #1186 asked
  for (#1193).

## [0.3.12] - 2026-08-25

### Fixed

- `-c=native`: `include/threads.h`'s `once_flag` re-materialized as
  `typedef int once_flag;`, which collides on Linux with glibc's own
  `once_flag` typedef (pulled in unconditionally by the `<threads.h>`
  serializer shim's own `#include <stdlib.h>`) — "conflicting types for
  'once_flag'" on every native compile touching `<threads.h>`. Fixed by
  renaming the type to `__cccc_once_flag`, with `once_flag` aliased onto it
  via a guest-side-only `#define`/`#undef` pair. The rename alone wasn't
  sufficient: the pair was still auto-captured and replayed verbatim into
  native output, staying live long enough to also rename glibc's OWN
  `once_flag` typedef via macro substitution and reopen the exact same
  collision one identifier over (confirmed on real Linux hardware, not
  reproduced in local container verification). Fixed by dropping the
  `#define`/`#undef` pair from native output's own directive replay
  entirely — every re-derived declaration already spells the real name
  directly, never through the macro (#1183).
- `-c=native`: fixing the `once_flag` collision above unmasked a second,
  distinct one underneath it — a sufficiently new glibc declares its own ISO
  C11 `call_once()` straight off `<stdlib.h>` (no `<threads.h>` needed),
  colliding with `<threads.h>`'s shim `call_once` definition on differing
  parameter types ("conflicting types for 'call_once'"). Fixed the same way
  as the type: the shim function is privately named `__cccc_call_once`, with
  `call_once` aliased onto it and dropped from directive replay identically
  (#1183).
- `include/stdlib.h` (the bundled header replayed into every native
  `#include <stdlib.h>`) used `//` line comments, illegal under strict ISO
  C90 — any native compile under `--std=c89` that pulled it in failed with
  "C++ style comments are not allowed in ISO C90" on host compilers that
  reject them outright (not reproduced locally; both clang and the Colima
  verification container's GCC accept `//` as an extension even in
  `-std=c89` without `-pedantic-errors`). Converted to `/* */` (#1186).

### Added

- `tools/tests.py --native` (the `-c=native` serializer round-trip corpus)
  is now wired into `tools/run_tests.py` as an on-by-default sub-suite
  (`--no-native` opts out), so a serializer regression is caught on every
  ordinary push instead of accumulating unnoticed (#1157; advisory as of
  #1186, see Known issues below).
- `tools/tests.py --native-audit-skips`: a behavioural audit mode that
  bypasses the `NATIVE_SKIP_TESTS`/`NATIVE_SKIP_TESTS_MACOS`/
  `NATIVE_SKIP_TESTS_LINUX` skip tables for just the files they name, runs
  them for real, and reports any entry that now passes as stale. Wired into
  `run_tests.py` as its own sub-suite (advisory as of #1186, see Known
  issues below). Found and removed three stale entries in this pass
  (#1182).

### Known issues

- `stdatomic.h`'s `atomic_fetch_add`/`_sub`/`_or`/`_xor`/`_and` (and their
  `_explicit` spellings) desugar to a non-atomic load-then-store, safe only
  under the VM's own GIL — a real, intermittent (~7-13%) data race under
  `-c=native`'s real parallelism. Found stress-testing the new native CI
  suite; `tests/test_threads_call_once_1088.c` is skipped citing this rather
  than a regression in its own `call_once` shim, which is correct (#1184).
- The `native`/`native_skip_audit` sub-suites added in #1157/#1182 turned
  out to be fragile against GCC-version differences: verified green on
  macOS/arm64 and inside the `cccc-linux-amd64` verification container, the
  first real push to sr.ht's own Linux amd64 build hardware surfaced five
  additional compile/runtime failures plus six false-positive `STALE`
  skip-audit findings, none of which reproduce locally — a different GCC
  version than the container's own. Downgraded both sub-suites from
  hard-blocking to advisory (`tools/run_tests.py`'s `_ADVISORY_SUITES`) as a
  stopgap: CI still reports both, neither reds out a push. Tracked in
  #1186, which also carries the per-failure detail and the acceptance bar
  for re-promoting the gate back to blocking.

## [0.3.11] - 2026-08-25

### Fixed

- `-c=native`/`-m`: a struct/union/enum referenced ONLY inside a
  `sizeof`/`_Alignof` expression, an array dimension, a `case` label, an
  enum value, or a `_Static_assert` (file- or block-scope) — const-folded
  to a plain integer literal at parse time, but sometimes re-materialized
  textually (`sizeof(T)` rather than the folded literal) when the type is
  host-owned — never got its own definition emitted: the type-collection
  traversal (`collect_node_types()`/`collect_type()`, `src/serialize_type.c`)
  never walked the five layout-provenance stashes those sites leave behind
  (`Node.layout_ty` and friends), so a re-materialized `sizeof(T)` could be
  the only surviving reference to `T` anywhere in the AST, and the host
  compiler rejected the output ("invalid application of 'sizeof' to an
  incomplete type"). Fixed by walking each stash, gated by the same
  host-owned/printable-name check `serialize_layout_const()` itself uses
  (so a type that stays folded, the common case, is never force-emitted —
  an earlier unconditional version of this fix regressed
  `tests/suites/test_suite_structs.c`'s own `tc_bi1135_wide`, a struct with
  a `_BitInt(129)` member that has no native/-m lowering at all) (#1167).
- `-c=native`/`-m`: `type_layout_is_host_owned()` (`src/serialize_type.c`)
  accepted any `Type` kind, not just struct/union/enum, so a plain scalar
  member or operand (e.g. `long`) could spuriously match an unrelated
  `from_include` typedef of the same builtin (e.g. `sys/types.h`'s
  `__int32_t`, reached merely by including some header) via the
  origin-chain pointer-identity walk `find_typedef_name()` falls back to —
  judging a wholly user-defined aggregate host-owned and re-materializing
  its `sizeof`/`_Alignof` textually instead of folding it to a plain
  literal, even though the type has nothing to do with any header. Fixed
  by restricting `type_layout_is_host_owned()` to `TY_STRUCT`/`TY_UNION`/
  `TY_ENUM`, mirroring the narrowing #1098's `expr_has_host_owned_layout()`
  already applied to itself for the same reason (#1168).
- `-c=native`/`-m`: follow-up to #1168 — restricting
  `type_layout_is_host_owned()` to `TY_STRUCT`/`TY_UNION`/`TY_ENUM`
  collaterally stopped a genuinely `from_include` **scalar** typedef whose
  real host layout differs from CCCC's own from ever re-materializing
  either (`sigset_t`: `unsigned int`/4 bytes in `include/signal.h`, 128
  bytes on glibc) — a native buffer sized off the guest-folded
  `sizeof(sigset_t)` under-allocated once handed to the real host
  `sig*set()` functions, reintroducing the #1031 hazard for it. #1168's own
  reasoning for not fixing this ("a scalar typedef's `Type` is
  origin-identical to its underlying builtin, so no identity check can
  distinguish `sizeof(sigset_t)` from `sizeof(unsigned int)`") turned out
  to be wrong: `parse_typedef()` already gives every non-aggregate typedef
  its own `Type` identity (`copy_type()`), and `find_typedef_name_exact()`'s
  directional `->origin` walk can already tell them apart — the same
  identity `serialize_type()`'s scalar-alias arm already relies on to spell
  e.g. `uint64_t` by name. Fixed with a non-aggregate arm in
  `type_layout_is_host_owned()` keyed on that identity lookup instead of
  `same_type_or_origin()`'s structural fallback, so #1168's spurious-match
  bug (a bare `long` matching an unrelated typedef of `long`) cannot
  reopen. Reached through a struct member too (e.g. `struct sigaction`'s
  own `sa_mask`), matching the existing aggregate behavior; `va_list`/
  `jmp_buf`'s compiler-owned safe-upper-bound carve-out is unaffected
  (#1169).

## [0.3.10] - 2026-08-25

### Fixed

- `include/dlfcn.h`'s `RTLD_LAZY`/`RTLD_NOW`/`RTLD_LOCAL`/`RTLD_GLOBAL` were
  hardcoded at glibc's numeric encoding on every platform. macOS uses
  different values, and glibc's `RTLD_GLOBAL` (`0x100`) collides with
  macOS's own `RTLD_FIRST` — a guest asking for `RTLD_GLOBAL` on macOS
  actually passed `RTLD_FIRST` to the real host `dlopen()`, on both the VM
  and `-c=native` (`mode` is forwarded to the host `dlopen()` unchanged on
  both backends). Fixed by deriving every `RTLD_*` value from the real
  host `<dlfcn.h>` this `cccc` binary was built against
  (`init_dlfcn_macros()`, `src/preprocess.c`), the same pattern
  `init_errno_macros()` uses for `errno.h`, rather than hand-transcribing
  them. The full family is now available, gated per-platform (`RTLD_FIRST`
  on macOS; `RTLD_DEEPBIND`/`RTLD_BINDING_MASK` on Linux); a flag the host
  libdl doesn't have is simply undefined, so misuse is a compile error
  rather than a silently wrong integer (#1152).
- `tests/suites/test_suite_printf_c23.c` fails `sscanf("0B1111", "%B", &c)`
  under `-c=native` on Linux too, not just macOS — glibc's `scanf` has no
  `%B` conversion specifier at all (only lowercase `%b`, carried since
  2.38), unlike `printf` where `%b`/`%B` are interchangeable. Moved from
  the macOS-only native skip table to the general one; documented as a
  permanent platform gap alongside the existing macOS-only gap (#1162).
- `VarScope` (`src/parse_internal.h`) and `VarScopeNode` (`src/cccc.h`) had
  silently diverged in layout — #1095 appended two fields to `VarScope` and
  never mirrored them onto `VarScopeNode`, though every scope-push
  allocation casts a `VarScopeNode*` to `VarScope*`. The two fields aliased
  `VarScopeNode`'s own `name`/`name_len`, so two or more `EnumAddConstant`
  calls on a comptime-generated enum corrupted an earlier constant's own
  name — silently on the VM path, a segfault under `-c=native` (the
  corrupted `char*` was read back as a `Type*` by the serializer). Fixed by
  restoring the full field-prefix match and adding a `static_assert` so the
  two structs can't drift apart again unnoticed (#1155).
- A captured `#include`'s operand tolerated incidental internal whitespace
  under CCCC's own tokenizer (`#include @shared < glob.h>`) but replayed
  that whitespace verbatim into `-c=native`/`-m`/`-c=generated` output,
  where a real host cc's preprocessor treats the whole `<...>`/`"..."` span
  as one opaque token and fails to find a file literally named `" glob.h"`.
  Now normalized at capture time (#1155).
- `reallocarray()` calls now round-trip through `-c=native` on hosts whose
  libc doesn't provide the symbol (macOS) via an inline serializer shim,
  rather than failing to link — see `man/COVERAGE.md`'s reallocarray entry
  for how this differs from #1028's earlier decision not to fix it (#1155).
- `tools/testing/native.py --native`: a `--testing` `[[cccc::test]]` suite
  file that also carries `CCCC_EXPECT_STDERR`/`CCCC_REJECT_STDERR` (a
  compile-time warning that doesn't fail any assertion) used to be routed
  to the single-file compile-and-link path instead of the
  `--testing=native` suite harness, failing at the linker for lack of
  `main()` even though the suite itself passes cleanly (#1155).
- `__attribute__((packed))`, a type-level `__attribute__((aligned(N)))`,
  and a member's own `_Alignas(N)` were retained on `Type`/`Member` but
  never re-emitted by `-c=native`/`-m`/`-c=generated`, so a native binary
  silently laid such a struct out as if the attribute were absent — the
  emitted C was even self-inconsistent (a `sizeof` folded against the VM's
  packed layout next to an unpacked struct definition). Now emitted,
  precisely when they change the layout beyond what the members alone
  produce (#1129).
- `-c=native`/`-m`/`-c=generated` emitted the bare `asm` keyword at both of
  its two emission sites (statement form and a declarator's `asm("symbol")`
  label) — a GNU alternate keyword GCC/clang both disable under a strict
  ISO `-std=cNN`, turning it into a syntax error on a real host compiler.
  Now emits `__asm__` at both sites; the parser also accepts `__asm__`/
  `__asm` in statement position, not just `asm` (#1130). Fixing this
  surfaced a second, independent pre-existing bug it depended on to
  verify: an `asm("symbol")`-labeled function *definition* (not a
  standalone declaration) always re-emitted the label directly on the
  definition line too, which GCC/clang both reject regardless of `-std=`
  or keyword spelling — the label is now only emitted on the standalone
  declaration the "prototypes before bodies" pass already produces ahead
  of every definition.
- `-c=native`'s #1143 fix (directory-wide `-idirafter` demotion of a `-I`
  entry that resolved any of CCCC's bundled std headers) swept in two
  headers that were never meant to hand off to the real host at all —
  `math.h`/`float.h` (no `#include_next` of their own; the real host's
  copy doesn't declare the C23 IEEE family `fmaximum`/`setpayload`/etc the
  way CCCC's bundled copy unconditionally does) and `unistd.h` (real glibc
  puts `mkstemp` in `<stdlib.h>` instead). Both regressed from a compile
  that worked (or failed only at documented link time on macOS, #1037) to
  "undeclared identifier". Fixed by forcing CCCC's own `math.h`/`float.h`
  via an absolute-path `#include` substitution when a bundled directory
  was actually demoted, and adding `unistd.h` → `stdlib.h` as a fourth
  companion-include entry alongside #1143's existing three — except when
  the real host's `<tgmath.h>` was already replayed earlier in the same
  file, where forcing CCCC's copy corrupts its type-generic macros instead
  (regression from this same fix, caught before landing).

## [0.3.9] - 2026-08-24

### Added

- `tools/testing/header.py`: shared, anchored, whole-header-block parser for
  `tests/**/*.c` `CCCC_*`/`EXPECT_*` directive comments, replacing three
  independent 5-line-window substring scans (#1153). A directive on line 6+
  (pushed past the old fixed window by longer prose above it) used to be
  silently never read — the assertion it named became vacuously true.
- `tools/audit_test_headers.py`: hard-fails the `test` build target if a
  known directive appears unanchored (wrapped or merged into prose), after
  the header block ends, misspelled, or with an empty/non-regex value — the
  recurrence guard for #1153.
- `.clang-format`'s `CommentPragmas` excludes every directive comment from
  line-wrap reflow, so a future `clang-format` pass can no longer silently
  truncate a `CCCC_FLAGS`/`CCCC_EXPECT_STDERR`/etc value onto an unread
  continuation line (#1153).
- `tools/testing/test_header_parse.py`: unit tests for the new parser.
- `tests/test_builtin_str_mem_no_include_1154.c`: asserts
  `__builtin_strlen`/`strcmp`/`memset`/`memcpy`/`memmove`/`memcmp` serialize
  with their literal `__builtin_` spelling and never the bare libc name,
  with no `#include` at all.

### Fixed

- ~30 `tests/**/*.c` files whose header directives were silently damaged by
  the #1153 parser gap (a directive past line 5 never read, a directive
  wrapped or merged into prose by a stale `clang-format` pass, or a repeated
  `CCCC_EXPECT_STDOUT:` silently discarding all but the last occurrence) —
  every affected assertion, flag, or skip annotation is now actually
  enforced.
- `__builtin_strlen`/`__builtin_strcmp`/`__builtin_memset`/`memcpy`/
  `memmove`/`memcmp` used to serialize under `-c=native`/`-m`/`-c=generated`
  as a plain call to the bare libc name with no declaration, needing the
  caller's own `#include <string.h>` even though the VM path and real
  GCC/clang both need none. `serialize_expr.c`'s `ND_VAR` case now prints
  all six using their literal `__builtin_` spelling, the same way it
  already did for `__builtin_alloca` (#1154).
- `__has_builtin(__builtin_memcpy)`/`memset`/`memmove`/`memcmp` returned
  false while the builtins themselves worked — `#1144` added the parse arms
  but never added them to `is_has_builtin_supported()`'s table.

## [0.3.8] - 2026-08-24

### Added

- `tests/test_implicit_function_error_1144.c`/`test_implicit_function_c89_
  1144.c`: implicit function declaration is a hard error at the default
  std, a warning at `--std=c89`.
- `tests/test_stdio_posix_decls_1144.c`: the newly-declared `<stdio.h>`
  POSIX functions, VM and `-c=native`.
- `tests/test_builtin_mem_1144.c`: `__builtin_memset`/`memcpy`/`memmove`/
  `memcmp`, VM and `-c=native`.

### Changed

- Implicit function declaration (a call to a function with no visible
  declaration anywhere) is now a hard compile error at `--std=c99` and
  later — CCCC's own C23 default — matching ISO C99 6.5.2.2p1 and every
  real host C compiler. It stays a warning only at `--std=c89`/`gnu89`.
  Under `-c=native` it is always a hard error regardless of `--std=`,
  since the guessed implicit signature is never emitted into the
  generated C. Previously CCCC accepted it silently at every standard
  (the call resolves against the FFI registration table purely at VM
  codegen, needing no declaration at all), which is what let
  `tests/suites/test_suite_posix.c` compile on the VM while failing
  outright under `-c=native` with "use of undeclared identifier
  isalpha_l" — the root cause was implicit function declaration, not (as
  first suspected) any FFI-table-based resolution in the parser itself.
  An audit of every registered FFI cfunc against every bundled header's
  own declarations found and fixed one real gap: `popen`/`pclose`,
  `fseeko`/`ftello`, `flockfile`/`ftrylockfile`/`funlockfile`, and
  `getc_unlocked`/`getchar_unlocked`/`putc_unlocked`/`putchar_unlocked`
  now have real declarations in `<stdio.h>`.
- `__builtin_memset`/`__builtin_memcpy`/`__builtin_memmove`/
  `__builtin_memcmp` are now recognised, forwarding to the real libc
  functions of the same name the same way `__builtin_strlen`/
  `__builtin_strcmp` already did — needing no `#include <string.h>` on
  the VM path, matching real GCC/clang.

### Fixed

- `-c=native`: `sysconf`/`pathconf`/`fpathconf`/`confstr` passed CCCC's own
  canonical, host-independent `_SC_*`/`_PC_*`/`_CS_*` numbering straight
  through to the host's real functions with no translation, silently
  asking for the wrong thing on any host whose numbering doesn't already
  match CCCC's (e.g. guest `_SC_PAGESIZE`, 11, reached the host as literal
  `11`, not macOS's 29 or glibc's 30). Fixed with translating
  `__cccc_native_sysconf`/`pathconf`/`fpathconf`/`confstr` wrappers
  (`serialize_canonical_const_shims`) ported from the VM's own
  `wrap_sysconf` family, including the `_SC_VERSION`/`_SC_2_VERSION`/
  `_SC_XOPEN_VERSION` VM-model-constant special case.
- `-c=native`: `SCHED_BATCH`/`SCHED_IDLE` and `ppoll()` are glibc
  extensions gated behind `_GNU_SOURCE`/`__USE_GNU`, which the generated
  translation unit never defines — both failed to compile at all on
  Linux. Fixed by locally supplying the missing macro/declaration, the
  same policy every other native POSIX shim already follows rather than
  flipping on `_GNU_SOURCE` TU-wide.
- `-c=native`: `struct in6_pktinfo` has the same `_GNU_SOURCE` gap on
  Linux for a type rather than a function — fixed the same way, by
  force-emitting the struct's definition under a guard that's a no-op
  wherever the host already provides one.
- `-c=native`: a local variable declared `posix_spawnattr_t`/
  `posix_spawn_file_actions_t` (opaque pointer-width handles on the guest,
  real ~336/80-byte structs on glibc) lost its typedef alias during
  serialization and was emitted as a bare `void *`, so the real host
  `posix_spawnattr_init()` etc wrote a real-sized struct into an 8-byte
  stack slot — silent stack corruption. Fixed with
  `serialize_local_var_type_decl`, narrowly scoped to local-variable
  declarations only.
- `-c=native`: `aio_fsync(op, NULL)` segfaulted instead of returning
  `-1`/`EINVAL` — the VM's own NULL-`aiocbp` guard never had a native
  counterpart. Fixed with a native wrapper reproducing just that guard.
- `tests/suites/test_suite_posix.c` is back on the native test corpus —
  all of the above, plus two test-side fixes for assertions that only
  ever held under the VM's own model (an arbitrary `sa_flags` bit pattern
  round-tripping through `sigaction()` verbatim; `SI_USER` for a
  self-raised signal), not on a real kernel.

## [0.3.7] - 2026-08-23

### Changed

- Split `src/serialize.c` (12,719 lines, ~200 static functions feeding a
  single `cc_serialize_program` entry point) into six files by section
  (`serialize_type.c`, `serialize_expr.c`, `serialize_stmt.c`,
  `serialize_decl.c`, `serialize_shims.c`, `serialize_program.c`) plus
  `serialize_internal.h` for the shared context structs and cross-file
  prototypes, mirroring the earlier codegen/parse splits. Purely
  mechanical — no behavioral changes.

### Fixed

- `-c=native`/`-m`: a global initializer for a wide-`_BitInt`-typed
  bitfield member (e.g. `_BitInt(128) f : 100;`) silently dropped any bit
  at or above bit 64 of the field's value — `serialize_init_bytes`'s
  bitfield-value re-extraction clamped its read to 8 bytes regardless of
  the member's own container size and printed a plain `%llu` literal.
  Fixed with a byte-granular extract over the field's exact bit span
  (mirrors the `__cccc_bitfield_extract` runtime helper), sign extension,
  and a 128-bit hex literal for values that don't fit a `long long`.
  `tests/suites/test_suite_typesystem.c` is back on the native test corpus
  as a result (#1126).
- `-c=native`: `dlclose` forwarded straight to the host's libdl, which
  doesn't enforce the VM's own "refuse to close a handle with a still-live
  `dlsym`'d symbol" policy — a valid handle closed successfully natively
  where the VM refuses it. Fixed with a registry shim
  (`serialize_dlfcn_shims`) reproducing the VM's own dynamic-library
  bookkeeping over the replayed real `<dlfcn.h>`. `tests/suites/test_suite_ffi.c`
  is back on the native test corpus as a result (#1105).
- `-c=native`: a global initializer taking a libc function's address (e.g.
  `static FfiOps ops = {strlen, strcmp};`) emitted a second, conflicting
  prototype for that function — CCCC's own bundled-header spelling,
  colliding with the real one the replayed `#include` already supplied.
  Fixed by giving the reloc-forward-declare pass the same header-supplied
  suppression the ordinary function-prototype pass already had (#1151).

## [0.3.6] - 2026-08-23

### Fixed

- `-c=native`: a user `-I`/`-isystem` entry that also happened to hold
  CCCC's own bundled headers (this repo's own test harness's
  `-I./include` is exactly that) shadowed the real host headers a
  `#include_next` hand-off (`pthread.h`, #1022, and everything it
  transitively reaches) needs to see — confirmed to collide with a real
  host `<pthread.h>` in the same TU as `<sched.h>`/`<locale.h>` (`struct
  sched_param`/`struct lconv`/`locale_t`/`freelocale` redefinitions,
  undeclared `SIG_SETMASK`/`htonl`, static-vs-extern `gethostbyname_r`).
  Fixed by demoting exactly the `-I`/`-isystem` entries that actually
  resolved one of CCCC's own bundled headers
  (`cc_include_dir_is_cccc_bundled`, `src/preprocess.c`) to `-idirafter`
  when forwarding to the host compiler (`run_native_backend`,
  `src/main.c`), so the real host header always wins the search while a
  header the host genuinely lacks still resolves as a last resort
  (#1143). Un-masked, and fixed alongside: three of CCCC's own bundled
  headers quote-`#include` a second, related standard header purely as a
  convenience the real, minimal host header doesn't replicate (`fts.h` →
  `sys/stat.h`, `unistd.h` → `sys/uio.h`, `sys/un.h` → `sys/socket.h`) —
  the include-replay loop now emits the real header's own `#include`
  alongside the replayed line so this convenience keeps working once the
  real host header is reached; and the `mtx_timedlock` macOS shim's own
  hand-rolled `extern int clock_gettime(int, struct timespec *)` (wrong
  on macOS, where `clockid_t` is a real enum, not `int`) is dropped now
  that the real host declaration is reliably reachable through the same
  `<pthread.h>` hand-off this shim already requires.

## [0.3.5] - 2026-08-23

### Fixed

- `-c=native`: `ppoll`, `sched_setparam`/`getparam`/`setscheduler`/
  `getscheduler`/`rr_get_interval` (`--posix-emulation`), and
  `gethostbyname_r`/`gethostbyaddr_r`/`getnetbyname_r` (ungated) were
  undeclared on a host with no real primitive for them (macOS for all of
  these) — "use of undeclared identifier". Fixed by porting the VM's own
  emulation/shim/stub for each into the emitted C
  (`serialize_posix_compat_shims`, `src/serialize.c`), guarded so a real
  host symbol is always preferred where one exists. A companion
  native-mode `<xlocale.h>` injection declares the `isalpha_l`/`toupper_l`/
  `nl_langinfo_l`/`strfmon_l` family, which macOS declares but not from the
  headers a plain `#include <ctype.h>`/`<langinfo.h>`/`<monetary.h>` pulls
  in (#1140).
- `-c=native`: CCCC's own canonical POSIX constant numbering
  (`POLLWRNORM`/`POLLWRBAND`, `nl_item`, `LC_*`/`LC_*_MASK`, `SCHED_*`) was
  never translated to the host's real values the way the VM's own wrappers
  do — every guest use was already folded to a plain integer by parse
  time, so the emitted C passed it straight to the host function with no
  translation and no diagnostic, silently wrong on whichever host's real
  numbering isn't what CCCC's canonical numbering happens to copy (`poll`/
  `sched_get_priority_min`/`max`: wrong on macOS; `nl_langinfo`/
  `setlocale`: wrong on Linux). Fixed by renaming the guest's own
  declared-only reference to `__cccc_native_<name>` and supplying a
  translating wrapper under the new name
  (`rename_bundled_extern_for_native_shim`/
  `serialize_canonical_const_shims`, `src/serialize.c`);
`ppoll`'s existing shim now translates too,
    so it stays consistent with        plain `poll()` in the same binary
            .The emitted `gethostbyname_r`/`gethostbyaddr_r`/
  `getnetbyname_r` mutex now also covers the plain `gethostbyname()`/
  `gethostbyaddr()`/`getnetbyname()` family(only when a program uses both),
    matching the VM's own single shared mutex (#1146). - `-
        c = native` never linked `- liconv`,
        so a program calling
  `iconv_open()`/`iconv()`/`iconv_close()` failed at link time on
                macOS("Undefined symbols ... _iconv") — invisible until
                #1140 cleared the compile
            - time errors ahead of it.Fixed by appending `-
            liconv` to the native `cc` invocation on Darwin only,
        alongside the existing unconditional `- lm`/`- pthread`; glibc bundles `iconv` in libc itself, so
  Linux is unaffected either way (#1147).

## [0.3.4] - 2026-08-23

### Fixed

- `-c=native`: `FD_ZERO`/`FD_SET`/`FD_CLR`/`FD_ISSET` (`include/sys/select.h`)
  indexed `fd_set`'s storage through a member named `__fds_bits`, baked in
  by macro expansion at guest parse time — under `-c=native` the real host
  `struct fd_set` has no member by that name (macOS: `fds_bits`; glibc:
  `__fds_bits`, but a different word width), "no member named '__fds_bits'
  in 'struct fd_set'". Fixed by indexing through a plain
  `(unsigned char *)(set)` reinterpretation of the whole object instead,
  correct against both layouts (#1138).
- `-c=native`: a guest read/write of `environ` reached the output as a call
  to `__cccc_environ_ptr`, an internal accessor with no native definition —
  "use of undeclared identifier '__cccc_environ_ptr'". Fixed by adding it
  to `native_accessor_shims` (`src/serialize.c`) and narrowly guarding
  `include/unistd.h`'s own conflicting `extern` under `#ifdef __CCCC__`
  (#1139).
- `-c=native`: `<uchar.h>`'s C11/C23 multibyte↔UTF-16/32/8 conversions
  (`mbrtoc16`/`c16rtomb`/`mbrtoc32`/`c32rtomb`/`mbrtoc8`/`c8rtomb`) had no
  definition to link against on a host lacking the real symbols (Darwin
  has never shipped any of the six) — "Undefined symbols ... _c16rtomb".
  Fixed with a self-contained fallback shim (`serialize_uchar_shims`,
  `src/serialize.c`), a port of the VM's own `src/stdlib/wide.c` fallback,
  emitted only on a host that actually needs it (#1141).
- `include/wchar.h`'s `mbstate_t` typedef now guards itself under the same
  include-guard macro name each real host's own `mbstate_t` definition
  uses (glibc: `__mbstate_t_defined`; Darwin: `_MBSTATE_T`), preventing a
  future redefinition if some other header gains a hand-off that also
  reaches this translation unit's real `mbstate_t` (#1142).
- `-c=native`/`-m`: `rename_colliding_static_names()`'s host-libc-symbol
  probe (Tier A, #1042(c)) renamed a `static inline` header function's call
  sites without renaming its definition whenever that definition never
  actually reaches the output (it's supplied by the header's own replayed
  `#include` instead) — `include/ndbm.h`'s `static inline dbm_*` shims hit
  this for real on macOS, producing calls to an undeclared
  `dbm_store__cccc_dup4`-style identifier. Fixed by skipping the rename
  once `function_is_header_supplied()` is true for that `Obj` (#1103).
- `-c=native`/`-m`: a `{0}` initializer for a host-owned-layout local (e.g.
  `mbstate_t st = {0};`) stored a redundant zero through `include/wchar.h`'s
  own synthetic `__opaque` reserved-storage member, which doesn't exist on
  the real host `__mbstate_t` once the host's own `<wchar.h>` is in scope —
  "no member named '__opaque' in '__mbstate_t'". Fixed by dropping every
  zero-store into `__opaque` that's already covered by the initializer's own
  leading `memset`, and erroring loudly (rather than emitting a silently
  wrong offset) on a non-zero store through it; an ordinary host-owned
  type's real, POSIX-named members (`struct timespec`'s `tv_sec`/`tv_nsec`,
  etc.) are untouched (#1103).
- The data-segment/TLS-template allocator (`gen()`, `src/codegen_func.c`)
  placed every global and thread-local at a hardcoded 8-byte boundary,
  ignoring the object's own declared alignment — an explicit `_Alignas(N)`,
  or a type whose natural alignment exceeds 8 (`__int128`/wide `_BitInt`
  after #1135, or a 16/32/64-byte vector, #722). Nothing faulted (VM
  loads/stores are byte-granular), but the placed address itself was wrong,
  which matters for FFI calls into real host code and for the return-buffer
  pool a by-value struct/union/vector/wide-`_BitInt` return goes through.
  Fixed by rounding every such allocation site — including the return-buffer
  pool, `cc_load_module`'s cross-module data/TLS re-anchoring, and the
  per-thread TLS base allocation (now `posix_memalign`, not `malloc`) — to
  the object's own effective alignment, capped at 64 bytes (the widest any
  type requests today). `-c=native`/`-m` had the same gap from the other
  direction: an explicit `_Alignas(N)` was never re-emitted, so it silently
  reverted to natural alignment in generated C; `serialize.c` now emits
  `_Alignas(N)` wherever a declaration's alignment was explicitly widened
  (#1136). Local (stack-frame) variables still only get 8-byte alignment —
  not reachable from the calling convention's own fixed frame layout — and
  remain a known limitation, tracked as a follow-up.

## [0.3.3] - 2026-08-22

### Fixed

- `__int128`/`unsigned __int128` (and `_BitInt(N)` for `N` in `(64, 128]`,
  which `__int128` is sugar for) is now aligned 16 on every supported
  target, matching clang/gcc's own `__int128` everywhere and matching the
  `__int128` host container `-c=native`/`-m` always lowers a 16-byte
  `_BitInt` container to (`bitint_type()` previously capped every
  `_BitInt` alignment at 8 regardless of container width). This closes a
  parse-time `sizeof`/`_Alignof`-fold divergence from the layout of the C
  cccc itself emits — `struct {
    char     c;
    __int128 x; }` is now `sizeof 32`
  / `_Alignof 16`, not `24`/`8` — the same class of bug as #1127. Note
  this is a deliberate divergence from clang's/gcc's own *native*
  `_BitInt(65..128)` spelling on x86_64, which is align 8 there (though
  align 16 on aarch64); cccc keeps the one rule everywhere so its VM and
  native output never disagree with each other. `_BitInt(N)` for `N > 128`
  is unaffected: no host container exists to match, so alignment stays 8
  (#1135).
- A struct containing a bitfield now reserves each *named* bitfield member's
  full declared-type storage unit in the struct's own size/alignment,
  matching clang/gcc (`struct A {
    int f : 5; }` is now `sizeof 4` /
  `_Alignof 4`, not `1`/`1`) — an unnamed member (including width-0) stays
  pure padding and does not affect alignment, and `packed` structs are
  unaffected (#1127). Member offsets and bit-packing within the container
  were already correct; only the struct-level rounding was missing. This
  was more than cosmetic under `-c=native`/`-m`: `sizeof` folds at guest
  parse time to the old, undersized value, so a `malloc(sizeof *p)`
  allocation for such a struct was too small for the struct the host
  compiler itself lays out — a real heap overflow in generated code
  (confirmed via clang's own `-Walloc-size` diagnostic on the emitted
  output, and clean under `-fsanitize=address` after this fix).
- File-scope initializers needing a >8-byte scalar write no longer crash at
  parse time (#1122): `write_gvar_data`'s scalar tail only handled 1/2/4/8-byte
  writes, so any global of a `_BitInt(N)` with `N > 64` (including `__int128`),
  `long double`, or `_Complex` type hit `internal error at
  src/parse_init.c:1601` — under the plain VM as well as `-c=native`/`-m`,
  regardless of backend. The constant folder for such an initializer is now
  arbitrary-width, reusing the same `src/stdlib/wide_bitint.c` word-array
  helpers the VM itself uses for runtime `_BitInt(>64)` arithmetic, so a
  global's folded value always matches what an equivalent local would compute
  at runtime; `long double`/`_Complex` initializers (real-valued only — this
  compiler has no imaginary-literal syntax) got their own `write_gvar_data`
  arms, and the matching `-c=native`/`-m` serializer gap for `_Complex`
  globals was closed too. This also makes #1121's `serialize_init_bytes`
  `TY_BITINT` arm reachable for the first time — it was dead code until now,
  since `write_gvar_data` could never produce the bytes it reads.

  Two adjacent bugs found in the same code paths, fixed alongside it:
  a *narrow* global whose initializer merely contained wide arithmetic used
  to silently fold wrong rather than crash or error — `eval2` is `int64_t`
  end-to-end, so e.g. `(unsigned long long)((wide_expr) / 3)` computed the
  division in 64 bits and got a plausible-looking but incorrect answer with
  no diagnostic; `eval2` now delegates any wide-typed subexpression to the
  same arbitrary-width folder. And a `T f : 64` bitfield (a bit-precise field
  spanning its entire container) computed its mask as `1ULL << 64`, which is
  undefined behaviour and, observed on this host, evaluates to 0 — silently
  discarding the whole field on both the global-initializer read-modify-write
  path and ordinary runtime bitfield load/store (`src/codegen_expr.c`). A
  bit-precise bitfield wider than 8 bytes (e.g. `_BitInt(128) f : 100;`) in a
  global initializer is now read-modify-written as a word array too, instead
  of crashing at `read_buf`; and `eval_rval`'s `ND_VAR`/`ND_LABEL_VAL` arms no
  longer dereference a NULL relocation-label pointer for an address-of-global
  expression reached from a context with nowhere to put a relocation (a
  bitfield initializer, or `eval_wide`'s narrow-operand fallback) — both
  `(int)&global` inside a bitfield and `(__int128)&global` used to segfault
  instead of erroring; `not a compile-time constant (address of variable)` in
  `tests/test_wide_int128_addr_of_global_1122.c` pins both routes. The guard
  deliberately excludes `ND_MEMBER`, since `offsetof(T, m)`'s expansion
  bottoms an `ND_MEMBER`/`ND_DEREF` chain out at a null-pointer constant and
  must keep folding with no relocation label at all.

  A pre-existing, unrelated bug was found in passing and filed separately
  rather than fixed here: reading a bitfield member whose *container* type is
  itself a wide `_BitInt` (e.g. `_BitInt(256) f : 193;`) can crash at runtime
  for some bit-widths, independent of any global initializer (#1125).

## [0.3.2] - 2026-08-22

### Fixed

- `-c=native`/`-m` keeps function-local typedefs of anonymous aggregates
  distinct across scopes (#1116): a function-local
  `typedef struct {
    int width;
    int height; } TdSize;` serialized neither its
  struct body nor its alias whenever a structurally identical anonymous
  aggregate typedef existed in a sibling function (or the type otherwise
  merged with a same-shaped one collected earlier) — the global collection
  pre-pass runs with no current function, so the nominal-distinctness check's
  exact-pointer alias lookup was visibility-filtered and saw no function-local
  record on either side, declared the pair "not distinct", and the second
  Type collapsed into the first's definition slot; the losing function then
  referenced an undeclared alias ("use of undeclared identifier 'TdSize'").
  The same asymmetry also merged a file-scope tagged struct with an identical
  same-tag function-local shadow, so only the local definition ever printed
  and every other function saw an incomplete type. Nominal identity is now
  resolved through unfiltered exact-pointer record lookups (a declaration's
  owner must not depend on which scope happens to be asking), complete
  same-tag aggregates whose defining tag records carry different owners are
  treated as the distinct types they are (each emitted under its own scope,
  legal shadowed redefinition), and `type_decl_owner()` prefers
  pointer-identity matches over structural ones so split definitions resolve
  to their own owner. This surfaced a pre-existing, previously unreachable
  native bug in `test_int128`'s own right — `__int128` multiply/divide
  produce wrong results beyond 64 bits natively while the VM is correct —
  now tracked as #1121, with the suite retagged to it.
- `-c=native` emits host definitions for the cccc-internal complex accessors
  (#1117): spelled complex calls that survive into the generated text as
  ordinary identifiers (`fabs`, `carg`, `creal`, ...) are expanded by the
  HOST compiler through the replayed `#include <complex.h>` /
  `#include <tgmath.h>` — which resolve to CCCC's own bundled copies, since
  the native backend forwards the guest `-I` paths — whose type-generic macro
  bodies reference VM-only builtins (`__cccc_creall`/`__cccc_cimagl` via
  tgmath's long-double-complex arms, and likewise every double/float arm),
  so the host compile died on "use of undeclared identifier '__cccc_creall'".
  A new demand-gated synth pass emits static inline definitions of the whole
  family — the nine `__cccc_creal/cimag/conj {
    , f, l}` accessors plus the three
  `__cccc_cmplx {
    , f, l}` constructors the replayed `CMPLX()`/`I` macros reach
  — mapped onto the host's own `__builtin_creal*`/`cimag*`/`conj*`/
  `complex`, whenever a captured include resolves to bundled complex.h or
  tgmath.h, any complex-typed object is reachable, or any helper name itself
  appears (private-header parses are deliberately never replayed). This
  un-skips `test_suite_floats.c` from the native corpus.

- `-c=native`/`-m` output stops replaying captured `#embed` directive lines
  (#1114): auto-capture records every top-level directive verbatim, so a
  guest file's `#embed` lines were re-emitted into the serialized C for the
  host compiler to re-evaluate — where the filename operand re-resolved
  against the native compile's own temp directory instead of the original
  source file's directory ("'../embed_data/test_data.bin' file not
  found"), and would stay broken even with an absolute path, since a
  host's expansion of a top-level `#embed` is a bare byte list with no
  initializer context. The directive's whole effect already happened at
  parse time (the spliced bytes are in the AST), so the replay was pure
  duplication; it is now dropped like the other non-replayable captures,
  with `--emit-cccc` exempted. This un-skips `test_suite_embed.c` from the
  `tools/tests.py --native` corpus.
- `-c=native` serializes global initializers of empty (0-byte) unions
  (#1115): the union-initializer path reconstructs through the largest
  member and refused unions with no members at all ("cannot serialize
  initializer ... union has no member spanning the full 0-byte object");
  they now emit an empty brace initializer, matching VM semantics exactly.
  Normal unions keep the largest-member path; covered by the new
  `tests/test_native_empty_union.c` in both backends. `test_suite_misc.c`
  itself stays off the native corpus — the fix let it get further into the
  native compile than before and unmasked two independent residual
  blockers, now tracked as #1118 (auto-captured `#define`/`#undef` lines
  with non-ASCII macro names are rejected by hosts under the std ladder's
  spelling) and #1119 (inline `asm(...)` serializes verbatim to the host
  assembler; the suite's fake/x86 mnemonics assemble nowhere).
- `-c=native`/`-m`/`-c=generated` output compiles around C23 `nullptr_t`
  (#1111): implicit conversions into `nullptr_t` — assignment conversion, or
  null-pointer-constant equalization in comparisons — spelled their cast
  destination through the bundled `<stddef.h>` typedef name
  (`np = (nullptr_t)0;`), but casting *to* `nullptr_t` is not valid C23
  syntax even where assignment/conversion would be, so every host compiler
  rejected it outright. Cast destinations now emit `(void *)` — the host
  `nullptr_t` is `typeof(nullptr) == void *`, same size/representation, so
  every assignment/comparison keeps its meaning; declarations of `nullptr_t`
  objects keep their typedef name.
- Postfix tails after a compound literal (`(struct P){30, 12}.x`,
  `(int[]){1,2,3}[0]`, `((struct T *){p})->m`) now parse (#1112): C99 6.5.2p5
  binds `.member`/`[index]`/`->member` tighter than the literal itself, but
  postfix() returned directly from its compound-literal branch without
  running the shared tail loop, so every bare form was a syntax error
  ("expected ','") unless the literal was parenthesized. The literal now
  falls through to the same tail loop as any other primary expression;
  block-scope literals keep their comma-chain lowering and file-scope/
  static literals their anonymous-global one, so codegen is untouched.
- `-c=native`/`-c=generated` output compiles again around three spellings
  surfaced by #1033's native-corpus sweep of `tests/suites/` (#1102):
  - Address-of a block-scope compound literal (`&(struct P){30, 12}`) no
    longer emits `&(memset(...), t.x = 30, t)` — C's comma operator never
    yields an lvalue, so every host compiler rejected that outright
    ("cannot take the address of an rvalue"). The `&` now binds to the
    chain's addressable tail: `(memset(...), t.x = 30, &t)` — including
    when a postfix shell sits above the chain, as in `&((struct P){40,
    41}).x` or a pointer-typed literal's `->y`: `(..., &t.x)` and
    `(..., &(*t).y)` respectively.
  - `-(-5)` — typically a macro like `#define abs(x) ((x) < 0 ? -(x) :
    (x))` expanded on `-5`, or a `_Generic`-selected arm — serialized flat
    as `--5` and re-lexed as pre-decrement ("expression is not
    assignable"). The inner negation keeps its parentheses; all other
    unary spellings are unchanged.
  - A const-element aggregate local (`const int a[3] = {1,2,3}`) hoists to
    a declaration plus per-element assignment statements (#1029's scheme),
    but C spells the qualifier on the element type one level below where
# 1029 stripped it, leaving a genuinely - const object that the
    assignments stored into ("read-only variable is not assignable" from
    clang, which rejects any statically-const store). The hoisted
    declarator and the byte-offset cast-back both drop the element
    qualifier now.
  This un-skips `test_suite_structs.c`, `test_suite_std_c11.c` and
  `test_suite_std_c99.c` from the `tools/tests.py --native` corpus;
  `test_suite_c23.c` stays skipped pending #1104 (`_Decimal` lowering).
- `--testing` (VM/`=bytecode` backends) combined with `-c=bytecode` or
  `-c=native` now really compiles after the suite passes (#1106): the tests
  act as a pre-pass guard, and only a fully green run reaches the compile
  step (`-c=bytecode` writes `-o FILE`/`a.c4`, `-c=native` builds through
  the host toolchain). Any failing test exits nonzero without producing an
  artifact — independent of `--fail-fast`, which only stops the test run
  early. The same guard now applies to `--testing --build`: a build whose
  suite fails is refused before any target compiles (previously only
  `--fail-fast` stopped it). `-c=generated` still combines with `--testing`
  unguarded: its serialization runs before the suite (existing behaviour,
  relied on by tooling).
- `-c=native`/`-c=generated` atomic operations against `_Atomic`-qualified
  lvalues now compile (#1101): the serializer emitted `&x` straight into
  `__atomic_load_n`/`__atomic_store_n`/`__atomic_exchange_n`/
  `__atomic_compare_exchange_n`, but declarations spell through the host
  typedef names (`atomic_int x;` is the real `<stdatomic.h>` `_Atomic int`),
  so clang rejected every such call ("address argument to atomic operation
  must be a pointer to integer or pointer"). Address operands now cast to
  their pointee with the qualifier stripped — `(int *)&x`, `(_Bool *)&f` for
  `atomic_flag` — leaving the object itself genuinely `_Atomic`; non-atomic
  operands serialize byte-identical to before. This un-skips
  `test_suite_atomics.c` and `test_suite_optimizer.c` from the
  `tools/tests.py --native` corpus (the two files #1033's sweep flagged).
- `-c=native`/`-c=generated` output no longer depends on which
  `<stdatomic.h>` the host compiler resolves (#1109): `atomic_flag` was
  spelled through the bundled typedef name, but a real host header defines
  it as a *struct* (C11 7.17) while cccc's own header makes it an integer
  flavour of `_Atomic _Bool` — so every integer-style use failed to compile
  wherever cccc's headers weren't first on the include path (the native test
  harness always passes `-I./include`, plain `-c=native` runs don't).
  Serialized output now always emits the canonical `_Atomic _Bool` for
  declarations and casts; output compiled against either header is
  unchanged.

## [0.3.1] - 2026-08-21

### Added

- URL `#embed` — `#embed <https://...>` and `#embed "https://..."` now fetch
  the file into the same cache URL `#include` uses (`--url-cache-dir`,
  default `$TMPDIR/.cccc`) and embed the fetched bytes; `limit()`,
  `prefix()`, `suffix()`, and `if_empty()` apply to the fetched data exactly
  as they do for local files, and the existing large-embed warnings/limits
  still govern the result. Like URL includes this requires a curl-enabled
  build (`CCCC_HAS_CURL=1 ./cccc --build build.c`) and is a compile error
  otherwise. The tokenizer now preserves `//` inside `#embed <...>` paths
  (it previously only did so for `#include`, so an angle-bracket URL was
  truncated at the first `//` and died with "expected '>' after #embed
  filename").
- Guest-visible feature predefine `__CCCC_HAS_CURL__` — defined as `1` in
  curl-enabled builds (same republishing pattern as `__CCCC_HAS_NDBM__`),
  so code and tests can tell URL-capable builds apart.
- URL-aware `__has_include()`/`__has_embed()` probes (#1107): in curl-enabled
  builds both probes accept URLs and resolve through the same shared cache a
  real `#include`/`#embed` uses (cache-first, so an already-cached copy is
  probed without network I/O). They agree with each other by construction,
  and answer what the actual directive would do — closing the gap where
  `#include <url>` could succeed while `__has_embed(<same-url>)` reported 0.
  Non-curl builds report 0 for URLs from both probes (a binary without fetch
  support cannot truthfully promise one).
- `--url-timeout=SECONDS` and `--url-max-size=SIZE` (#1108): the previously
  hardcoded URL fetch knobs (30s timeout, 10MB payload cap) are now CLI-
  configurable in curl-enabled builds, following the existing
  `--embed-limit=SIZE` size-suffix style. Defaults unchanged. The cap now
  also applies to cache hits, not just fresh downloads, so the knob behaves
  the same whether or not the copy is already local; it stays independent of
  the `#embed` limits, which still govern the embed itself afterwards.

### Fixed

- `tests/test_url_include_basic.c` was doubly broken and could never have
  exercised its network path: its `#ifdef CCCC_HAS_CURL` guard tested a
  macro CCCC never predefined for guest code (the test always took its
  trivially-passing fallback), and its URL was misspelled with a space
  (`https: //...`) to dodge the old tokenizer bug above. The guard now
  tests `__CCCC_HAS_CURL__`, the URL is spelled correctly, and the test
  probes header macros instead of calling `stbsp_sprintf`, which has no
  implementation behind it in the VM (stb_sprintf's body lives in
  stb_sprintf.c).

## [0.3.0] - 2026-08-21

### Added

- `--testing[=vm|bytecode|native]` (#1033): the testing backend is now a
  single argument-taking selector instead of a boolean plus a separate
  `--test-c4` flag. Bare `-t`/`--testing` keeps today's default meaning
  (`=vm`, in-process); `=bytecode` replaces the retired `--test-c4`;
  `=native` serializes the `[[cccc::test]]` harness itself into the
  generated C (assert runtime, a table built from the compiled program's
  own test records, and a TAP-emitting `main()` that forks each test for
  isolation) and runs it as a standalone binary, closing the coverage gap
  where the entire `tests/suites/` corpus could never be exercised through
  `-c=native` at all. v1 scope: `[[cccc::test_setup]]`/`test_teardown`
  hooks and negative (`error=`/`expect_compile_error=`) tests are refused
  with a diagnostic (no fork-safe/host-compilable equivalent); a per-test
  `flags=` delta or `RET_STRUCT` return assertion is individually marked
  `SKIP` in the TAP output. `tools/tests.py --native`/`--c4` route
  `--testing`-flagged suite files through the new backends automatically.
  See man/TESTING.md's `--testing=native` section.

### Removed

- `--test-c4` (#1033): retired in favor of `--testing=bytecode`, part of
  the new `--testing[=vm|bytecode|native]` selector above.

### Fixed

- `-c=native`: found while building `--testing=native`'s generated harness
  (#1033) — a synthesized preamble that reaches for a standard POSIX
  header (`<setjmp.h>`, `<sys/wait.h>`) under `-I./include` resolves to
  CCCC's own bundled polyfill copy instead of the real host header, same
  class of hazard as #1022/#1054. The harness itself now declares only the
  handful of symbols it needs directly (`_setjmp`/`_longjmp` reusing
  `serialize_synth_setjmp_decls`'s own raw-extern pattern, `kill()`, and a
  hand-rolled `WIFEXITED`/`WEXITSTATUS`/`WIFSIGNALED`/`WTERMSIG`) rather
  than including either header.
- `tools/tests.py --native`: a serializer round-trip test mode, mirroring
  `--c4`'s bytecode round-trip. Compiles each eligible positive test with
  `-c=native`, confirms the compiled artifact exists, then runs it and
  checks its exit code against the VM run — catching the class of bug where
  `-c=native` compiles and runs cleanly but returns a different answer than
  the VM. Opt-in (not wired into CI yet); see man/TESTING.md's "Native
  round-trip mode" section.
- `-c=native` (#1107): an implicit argument-coercion cast to a *derived*
  pointer type built from a `from_include` scalar typedef — e.g.
  `pthread_t *`, the parameter type `pthread_create()`'s first argument
  coerces to — printed the fully structurally-decomposed spelling
  (`void **`) instead of the typedef's own name, because the cast-printing
  path only ever consulted the typedef table for the exact cast target, not
  a pointer built on top of it. Harmless when the decomposed and aliased
  spellings denote the same real host type (macOS's own `pthread_t` is a
  real pointer), but a hard `incompatible pointer type` compile error on
  glibc, where `pthread_t` is `unsigned long int` — found running the
  `tests/suites/` corpus through `--testing=native` for the first time (see
  `--testing[=vm|bytecode|native]` above) via `tools/comptime_native_smoke.py`
  case 118 on Linux CI. Fixed by spelling the alias directly at this one
  cast-expression site when the pointee resolves to a `from_include`
  typedef, whose real declaration is always already visible via the host's
  own `#include_next`.
- `-c=native`, Linux only: a replayed `#include <sys/mount.h>` doesn't bring
  `struct statfs` into scope on real glibc (the struct lives in
  `<sys/vfs.h>` there, not `<sys/mount.h>` as on macOS/BSD), so a
  re-materialized `sizeof`/`_Alignof`/`_Static_assert` of `struct statfs`
  hit "invalid application of `sizeof` to an incomplete type" — found by
  running `tools/comptime_native_smoke.py` in a real Linux/x86_64 container,
  not reproducible on macOS. Fixed by following a replayed
  `#include <sys/mount.h>` with `#include <sys/vfs.h>` when running on
  Linux (`cc_serialize_program`, `src/serialize.c`).
- `tools/audit_ffi.py`: a fixed 8-line lookback window for detecting a
  runtime-gated (`--posix-emulation`) FFI registration was one line short
  of covering `posix_sched.c`'s `sched_rr_get_interval` (the 5th and last
  registration in that block), producing a false-positive "declared
  conditionally but registered unconditionally" guard mismatch. Bumped to
  12 lines.
- `-c=native`: no real `<threads.h>` lowering existed at all —
  `thrd_create`/`mtx_lock`/`cnd_wait`/`tss_create`/`call_once`/etc. are VM
  cfuncs with no host libc symbol to link against, so a native binary
  calling one failed at the host linker with no CCCC-side diagnostic
  (#1088). Fixed with a self-contained shim (`serialize_threads_shims`,
  `src/serialize.c`) defining each function directly over the real host
  `<pthread.h>` already replayed by the `#1022` hand-off, rather than a
  `#include_next` hand-off onto a real host `<threads.h>` — CCCC's own
  `thrd_error`/`thrd_timedout`/`thrd_busy`/`thrd_nomem` encoding doesn't
  match glibc's, and Darwin has no `<threads.h>` at all, so a self-contained
  shim closes both platforms in one change. `call_once` is now a real
  function on both back ends (VM cfunc + native shim), backed by an atomic
  compare-exchange, rather than the guest-side macro it used to be — safe
  only under the VM's own GIL, and a genuine race under `-c=native`'s real
  parallelism.
- `-c=native`: a block literal defined inside a genuinely nested function,
  capturing a variable owned by that function's own ancestor, was rejected
  outright (`#1074` follow-up); now lowers correctly by registering the
  capture as an upvar of its real owner and reading it back through the
  same env chase an ordinary nested-function upvar reference uses (#1080).
  A `__block`-storage ancestor capture is supported too, via one extra
  level of indirection in the env field.
- A nested function defined *inside* a block literal, reading a variable
  owned by the block's own enclosing function, was a wrong-answer bug on
  the VM and an independent segfault under `-c=native` (both compiled
  clean) — a block's own `__static_link` slot holds its descriptor
  pointer, not a plain frame pointer, which broke the uniform static-link
  chase the moment it needed to hop through a block ancestor. Both back
  ends now terminate the chase at the nearest block ancestor and read the
  variable out of that block's own capture descriptor instead (#1081). A
  structurally similar but distinct shape — calling a nested function
  whose own parent sits beyond a block ancestor — is not fixed by this;
  both back ends now reject it with a diagnostic instead of miscompiling
  silently (#1081 residual, tracked separately as #1100).
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
  ordinary C out-parameter idiom (`void fill(int *out){
    *out = 42; }`).
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
# 1002).The "was this supplied by a replayed header" check compared
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
# 993). `Block_release(b)` falling back to a synthesized `free` prototype
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
# 897, #906, and #952, all discovered independently within a single week.
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
# 973). `& v[1] - & v[0]` on a 2 - D VLA(`int v[n][m]`) did not return `1`.
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
  element** (#977, follow-up to #973). `int v[n][m] = {
    {1, 2}, {3, 4}}` left
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
  `int v[n] = {
    ...}` outright -- now documented in
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
  `struct S {
    union {
        int   i;
        float f;
    };
    int tag; } r = {.i = 7, .tag = 1};`
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
# 929(reopened with this evidence).

## [0.2.6] - 2026-08-12

### Added

- **`return=` compound-literal test assertions now support nested
  struct/union/array fields and anonymous struct/union members** (#489,
  follow-up to #353). A field that is itself a struct or union can be
  asserted with a nested compound literal (typed `(struct T){
    ...}` or a
  bare `{
    ...}` whose type is inferred from the field), recursing to a
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
  before this fix: `extern int g; int f(void){
    return g;} int g=42;` read 0
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
  backslash escaping, and `$VAR`/`${
    VAR}` expansion** — the lexer
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
  removal and backslash escaping per POSIX, plus `$VAR`/`${
    VAR}` expansion
  from the process environment as a single non-re-split, non-globbed
  literal chunk. (#954)

- **The comptime declaration index no longer mis-names a declaration whose
  segment contains a fixed-size array inside an anonymous struct/union body,
  or a leading C23 attribute** — `segment_declarator_name()` (`src/macros.c`)
  finds a declaration's declared name by scanning forward for the first
  depth-0 `[` array-dimension group, but tracked `[`/`]` depth without
  tracking brace depth. A member array inside an anonymous struct/union body
  declared in the same statement as its own declarator (e.g. `typedef struct
  {
    char n[32]; } A;`) put that member's `[` at apparent depth 0, so the
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
  `byte_count()`/`bounds()` may now name a sibling member (`struct S {
    int  n;
    int *[[cccc::array, cccc::count(n)]] p; };`), resolved relative to
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
  static locals and compound literals (`(int[]){
    ...}`, `&(struct S){
    ...}`)
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
