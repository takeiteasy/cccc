# Self-Hosting Gate

Compiling cccc with itself.

## Self-hosting spike (#1132)

Not wired into any build target or CI job — a manual, occasional check, run
by hand when touching serializer/parser completeness, and intended as a
non-default release-time job once it passes cleanly. Targets **v0.1.0**
(the only version goal; v0.5.0 no longer applies after the release reset).
Stage (a) of #1132:
does `cccc -c=native` produce a working `cccc` binary from cccc's own
`src/*.c` + `src/stdlib/*.c`?

`-c=native` already accepts multiple positional source files and links their
ASTs into one merged program before serializing (the same multi-TU front end
`--build` bytecode targets use), so this does **not** need
[[cccc::build]]-as-compiler (a separate ticket, #1133) — it's a single CLI
invocation:

```sh
SDK=$(xcrun --show-sdk-path)   # or the CommandLineTools SDK path directly
cccc --use-system-headers -D_FORTIFY_SOURCE=0 -c=native \
  $(ls src/*.c src/stdlib/*.c | grep -v -e src/ops.c) \
  -I include -I src -i "$SDK/usr/include" -I "$SDK/usr/include/ffi" \
  --iframework "$SDK/System/Library/Frameworks" \
  --link-arg=-framework --link-arg=CoreServices \
  -l pthread -l iconv -l ffi -L /opt/homebrew/opt/libffi/lib \
  -o /tmp/cccc_selfhost
```

(Linux: drop `-i`/`--iframework`/the ffi `-I` and the CoreServices link args —
`pkg-config --cflags libffi` covers it the way `Makefile` does; drop `-l iconv`,
glibc folds `iconv` into libc. `src/watch.c` builds against inotify there.) Run it from the repository root: `src/std_table.c`'s `.incbin` paths are
relative to the working directory. `-i` is
CCCC's own `--isystem` short flag, not `-isystem` glued (getopt parses that
as `-i` with the literal argument `"system"`). `--sysroot` and `-i
"$SDK/usr/include"` are equivalent for this invocation.

`--use-system-headers` is required on macOS: `src/watch.c` (with `src/dmon.h`)
includes CoreServices, whose SDK headers cannot share a translation unit with
cccc's bundled `<signal.h>`/`<sys/*.h>`. Without it every file except
`src/watch.c` compiles, but the link fails on the `cc_watch_*` symbols
`src/main.c` calls. `tests/host/test_watch_selfhost.c` builds `src/watch.c` next to a
small driver and checks that a real file change is seen;
`tests/host/test_sdk_headers.c` covers `va_list`, `__signed` and the SDK-only
declarations under the SDK headers.

**Round 19 (macOS arm64): the whole-tree recipe above builds and links, the
self-hosted binary passes `--version`, `-E`, a VM run and a `-c=native` build,
every `tests/suites` suite passes in VM mode, its `-c=native` results match
`build/cccc`'s, and all `tests/test_*.c`/`tests/macros/test_*.c` pass.**

**Round 18 status: stages (a) and (b) pass, and the full
full test gate (run at the time against `/tmp/cccc_selfhost`) is green (0 / 1243
failures) — stage (c) is signed off.**

Round 18 fixed the round-17 blocker: a handful of files under
`src/stdlib/` whose whole purpose is translating CCCC's canonical guest
numbering to the *real* host's numbering (`posix_sched.c`'s
`sysconf`/`pathconf`/`fpathconf`/`confstr` wrappers and its
scheduling-policy translation, `posix_poll.c`'s `poll`/`ppoll` event-bit
translation, `locale.c`'s `setlocale`/`newlocale` category translation)
got CCCC's own bundled, canonically-numbered headers instead of the real
system headers when compiled as guest input under self-hosting —
collapsing every translation table to identity. `src/host_shadow_macros.h`
+ `src/host_values.c` restore the real host's value for every affected
name as a `__CCCC_HOSTV_<NAME>__` macro (the same transitive-injection
pattern `init_errno_macros`/`init_time_macros` already use for errno codes
and `CLOCK_*` ids), included by each affected file. A full audit of
`src/` found the fix needs to cover 24 macOS-divergent names plus two
further members of the same family a single-platform audit alone
wouldn't catch: `SCHED_OTHER`/`SCHED_FIFO`/`SCHED_RR` (a latent miscompile
no test previously caught) and `locale.c`'s `LC_*`/`LC_*_MASK` (a no-op
on macOS only because the bundled values happen to match there — it
would have re-opened #819 on Linux). Adding a translation
needs a `__CCCC_HOSTV_<NAME>__` entry in `src/host_values.c` and a matching
restore block in `src/host_shadow_macros.h` (`PATH_MAX`/`_XOPEN_VERSION` are
deliberately-divergent VM-model constants; `SCHED_BATCH`/`SCHED_IDLE` are
handled by `posix_util.h`'s own `#ifndef` fallback). See #1315 for the full
root-cause writeup.

Verified on Linux too (`cccc-linux-amd64` Colima container), since the
divergent-macro set differs by platform: a real self-hosted `-E` on `posix_sched.c` bakes
in `sysconf(30)` — glibc's real `_SC_PAGESIZE` enum value — not CCCC's
canonical `11`. One methodological wrinkle the tool's own comment
documents: glibc defines the whole `_SC_*`/`_PC_*`/`_CS_*` family as an
enum with each name re-`#define`'d to itself purely so `#ifdef` still
works, so a `-dM` textual probe can't read the enum's actual integer
value and the audit silently skips comparing it on glibc hosts — the fix
itself is unaffected (it compiles real C, not a textual scan), only the
audit's own regression coverage for that one family is narrower on
Linux than on macOS.

`--use-system-headers` (a whole-invocation flag) was not viable for the
82-file self-hosting build as a general fix, for two named gaps (#1318),
both since fixed: parsing the real SDK's `secure/_string.h` hit the same
`defined`-reached-via-macro-expansion bug #1319 fixed for `<pthread.h>`
(`-D_FORTIFY_SOURCE=0` is still required to skip that header's own
`_FORTIFY_SOURCE` macro rewrite — CCCC has no `__builtin___*_chk` — but no
longer to work around a preprocessor bug), and `-c=native` emitting invalid
C from the real SDK `<unistd.h>` (`unknown type name 'uint32_t'`) is fixed
via a compensating `#include <stdint.h>` (see
[headers.md](headers.md#real-sdk-headers-under---use-system-headers--sysroot)).
This was never a blocker for #1315 (fixed a different, narrower way), and
remains filed for awareness as the more general fix for that class of
problem — these two gaps closing doesn't mean every real-SDK-header quirk
is covered.

The round-16 stage-(c) blocker — a guest (non-FFI) function's incoming
`int` parameter reading as `0` in the self-hosted binary's VM
(`static int id(int a){return a;} id(42)` → 0, `fib(10)` → 0), while
no-argument functions, locals, and FFI arg passing were all fine — is
**fixed**. `serialize_expr.c` suppressed a same-signedness widening
integer cast everywhere as "always implicit in C", but a shift applies
the integer promotions to each operand independently and never the usual
arithmetic conversions, so `(long long)x << 32` and `x << 32` differ once
the cast widens past `int` (the second is a 32-bit shift, undefined for a
count of 32). `src/codegen_func.c` packs an `int` `spill_param_count` into
the `ENT3` instruction operand's high word exactly this way; with the cast
dropped the high word came out zero, `op_ENT3_fn` spilled no argument
registers to their parameter slots, and every parameter read back its
zero-initialised slot. A shift operand that is a would-be-suppressed
widening cast now keeps its explicit cast. Verified: pre-fix
`/tmp/cccc_selfhost` reproduced the ticket exactly; post-fix it runs
every reduction correctly (`id(42)` → 42, `fib(10)` → 55).

**The gate then surfaced two further, unrelated, pre-existing
self-hosted-binary blockers.** The first (#1314) is now also **fixed**:
an enum constant defined as a `|`-chain of *earlier same-enum
enumerators* whose values need more than 32 bits truncated each earlier
reference to `int` — `enum { A = 1ULL<<40, B = 1ULL<<2, ALL = A|B }`
folded `ALL` to `4` instead of `1099511627780` (verified against
gcc-16/clang, C17 and C23 alike, both of which fold it correctly with no
diagnostic). Reproduces under the plain VM too, not just `-c=native`:
`enum_specifier` (`src/parse_types.c`) selects an enum's underlying type
from its enumerator values only *after* the whole body is parsed
(#1175/#1205); while the body is still open, a reference to a prior
enumerator (`primary()`, `src/parse_postfix.c`) was typed with the enum's
not-yet-widened default `int`, and `eval2()`'s ND_CAST arm
(`src/parse_analysis.c`) truncated the wide value to 0. `src/cccc.h`'s
`CCCC_WARN_ALL` / `CCCC_WARN_EXTRA` are exactly this shape (`|`-chains of
`CCCC_WARN_*`, several past bit 31), so a self-hosted `cccc` lost every
`-Wall`/`-Wextra` flag whose bit was ≥ 32 — this alone drove 5 of round
17's original 8 gate failures. Fixed by widening the enumerator-reference
node to a type that actually holds its value whenever the not-yet-final
enum type is too narrow; a no-op once the enum is finalized or for any
enum whose members fit `int`. See #1314 and
`tests/test_enum_wide_prior_member_fold_1314.c`.

The second, round-17's `#1315` (self-hosting's host-macro-shadowing
hazard), is fixed in round 18 above.

**#1317** (child of #1132, filed for awareness off the back of #1315) asked
whether the *type-layout* analog of #1315's macro-value shadowing also
hits cccc's own source — `posix_statfs.c`'s `struct statfs`, `fenv.c`'s
`fenv_t`, `poll.h`'s `nfds_t`. Investigated and closed IMPLEMENTED: all
three self-correct (`fenv_t` is already host-sized via
`__CCCC_SIZEOF_FENV_T__`, `FE_DFL_ENV`'s sentinel is re-substituted by the
#1035 recognizer, `struct statfs` resolves against the real header on both
platforms, `nfds_t`'s width mismatch is real but benign) — but the
investigation found `include/sys/statvfs.h` missing the same
`#ifdef __CCCC__`/`#include_next` hand-off its sibling `sys/mount.h` got
for #1031, fixed to match. One methodological note worth keeping: `-E` is
the wrong probe for this class (it shows raw preprocessed text, before
serialization resolves the sentinel/layout) — use `-m` instead. See
[headers.md](headers-native.md#host-owned-typestruct-layout-under-self-hosting).

<details>
<summary>Round 17 and earlier history</summary>

Round 17 fixed the round-16 "guest params read as 0" blocker (#1312) and
an enum-truncation blocker (#1314), and root-caused (but had not yet
fixed) round-18's #1315: self-hosting compiles cccc's own host-facing
FFI-translation source (`src/stdlib/posix_sched.c`'s `wrap_sysconf`,
`src/stdlib/posix_poll.c`'s `wrap_poll`/`wrap_ppoll`) against CCCC's
*guest* bundled headers instead of the real system headers. Those files
rely on seeing the real host's own `_SC_PAGESIZE`, `POLLWRBAND`, etc. via
`#ifdef` (a normal `make` build always sees the real header — cccc never
processes its own source there), but self-hosting parses them as guest
input with `-I include` and no `--use-system-headers`, so
`is_compiler_owned_header()`/`force_cccc` (`src/preprocess.c`)
unconditionally resolves every standard header CCCC knows about —
`unistd.h`, `poll.h` included — to CCCC's own bundled copy.
`wrap_sysconf`'s `#ifdef _SC_PAGESIZE case CCCC_SC_PAGESIZE: return
sysconf(_SC_PAGESIZE);` compiled with CCCC's canonical `_SC_PAGESIZE`
(`11`) baked in instead of the real macOS value (`29`) — an untranslated
`sysconf(11)` reached the real host libc, which (on macOS) answers a
completely different question.

Round 16 fixed the round-15 stage-(c) blocker — a function parameter
named `optind` (or another bundled `<getopt.h>` accessor macro:
`optarg`, `opterr`, `optopt`, `optreset`) macro-expanded in declarator
position, turning `src/main.c`'s `build_source_argv(..., int optind, ...)`
into a bogus function-pointer parameter (SIGBUS natively, "invalid
indirect call target" under the VM). The parameter was renamed, and
`declarator()` now diagnoses any declarator name expanded from a bundled
host-accessor macro at parse time instead of letting it become a crash.
The self-hosted binary's VM then ran `int main(void){return 42;}` → 42
(round 15 SIGBUSed on it) and `--version`/`-E`/`-c=native` all worked,
but a guest function still read its own `int` parameter as `0` — the
round-17 fix above.

Round 15 fixed the round-14 stage-(c) SIGABRT (the linked binary aborted
on any invocation naming a file argument, inside `cc_init()`'s first
hashmap rehash): the serializer emitted the bundled `<assert.h>` macro's
comma-expression else-branch without parentheses, so
`expr ? (void)0 : puts(...) , abort()` re-parsed as
`(expr ? (void)0 : puts(...)) , abort()` — every `assert()` aborted
unconditionally, and `hashmap.c`'s `rehash()` holds cccc's only two
`assert()` sites. Past that fix the self-hosted binary ran `--version`,
`-E`, and `-c=native` (compiling and running a real `fib` program
correctly), but its VM interpreter SIGBUSed on any program because of the
round-16 `optind`-declarator blocker described above.

Round 14 cleared stages (a) and (b) for the first time: four more
`#include` capture/replay and header-supplied-suppression bugs (dedup-
relocation scans that were too broad, and a macro-expansion-site gate
that missed a token-pasted declarator whose macro is defined outside its
own header — `src/stdlib/format_printf.c`'s `stb_sprintf.h` is the real
shape that found all four). Stage (c) then failed with the SIGABRT the
round-15 note above describes as fixed.

Fails, past the perf wall AND #1286/#1287/#1290/#1291's
dropped-`volatile` finding. #1294/#1296's own five diagnostics — the
macro-shadowing half of #1291's round-12 libc batch (unlocked-stdio, the
checked-function `_chk` rewrites) and the setpriority/getpriority/catclose/
catgets/catopen declaration-shape batch — are fixed and confirmed gone by
isolated minimal repro (trunk, #1294/#1296's own commit); **the 82-file
spike itself has not been re-run past them**, so whatever clang's default
20-error cap was hiding behind these five is still unknown. cccc parses,
links, and serializes all ~82 files (~95k lines, not
the ~40k the ticket originally estimated) — the multi-TU linking and
serialization themselves are not the blocker. Round 8 cleared the batch
that stopped round 7 (`pthread_sigmask`, `clock_gettime`, `SIZE_MAX`,
`kill`, `strncasecmp`, `optreset`, plus `__atomic_compare_exchange_n` — see
below) and found two more, smaller frontend gaps past it: `f().member` on a
struct-returning call (`gen_addr` had no `ND_FUNCALL` case) and a single
function with more labels than codegen's internal table allowed (raised
256/1024 → 4096/16384). Past all of that, round 8/9 hit a genuine
performance wall — `same_type_or_origin()` (`src/serialize_type.c`), the
multi-TU type-identity predicate every type-registry lookup in that file
drives — that ran 60+ minutes at 100% CPU with no output.

That wall is now cleared (two commits): a hash-keyed candidate index
(`by_root`/`by_tag`/`by_fp`) narrows the ~20 `ctx->tags`/`ctx->typedefs`
scan sites; separately, and more importantly, `same_type_or_origin()`
itself now carries an equirecursive cycle guard generalized to *every*
recursive arm (struct member, function return/params, array element, not
just the original TY_PTR-only one) plus an `(a, b)` result memo spanning
the whole serialize pass. The real driver was two-fold: independently-
parsed copies of cccc's own heavily mutually-recursive Type/Node/Obj graph
recursed hundreds of frames deep through pointer pairs the old guard never
matched, *and* re-walked shared acyclic subgraphs exponentially with no
memoization. Verified sound by exact A/B: with the memo forced off
(`CCCC_TYPE_SAME_MEMO_DISABLE=1`) the serializer produces byte-identical
output on a 2-file reduction (962,551,517 same_type_or_origin() calls
collapse to 203,336 with zero behavioural change). At full 82-file scale:
37.1M calls, 70% served from the memo, max recursion depth 26 (was
unbounded/~500), wall ~20min total (was 60+min and never finished).

Past the wall, the spike hit two unrelated, pre-existing bugs the wall had
been masking (found separately — not part of the perf work): **#1286** — a
`Type *x = &(Type){...}` compound-literal global lost its backing
definition under `-c=native` (`src/type.c`, single-TU repro), and **#1287**
— a function-body `#include` of a local `.inc` file
(a spliced `.inc` fragment) got hoisted and duplicated at file scope by
the `#include`-replay machinery, losing the enclosing function's parameter
scope (`src/macros.c`, single-TU repro). Both **RESOLVED**: a compound
literal's synthesized anon backing global now carries a dedicated
`Obj.is_anon_synthesized` flag so `global_is_header_supplied()` never
misreads its inherited (and irrelevant) `obj->tok` provenance as "supplied
by a header"; the preprocessor now tracks brace depth over ordinary tokens
and excludes a `#include` seen at depth > 0 (inside a function body) from
the auto-capture/replay gate.

Re-running the **full 82-file spike** past both fixes confirms they hold
end-to-end, not just on their own single-TU repros: the run sails past
both error signatures entirely, and the type-stats line
(`total=37123419 toplevel=16985869 ratio=2.2 max_depth=26
cycle_hits=3707127 memo_hits=26125873`, wall **17m54s**) confirms the
`same_type_or_origin()` perf wall stays cleared as the build gets further
into the corpus (consistent with the ~19.7min post-wall-fix, pre-these-
two-fixes run). It then hit a **new** blocker, **#1290**: `TypeVec`
(`serialize_internal.h`) and the tagless `TypeKind` enum (`cccc.h`) each
got a real, replayed `#include` of their declaring header *and* a second,
from-scratch reconstructed definition, a straight "redefinition" from the
host compiler.

**#1290 RESOLVED** (`src/serialize_type.c`). The ticket's own suspected
cause (a `path_is_captured()` path-key collision from `cccc.h`'s two
`#include` spellings) was checked first and ruled out directly:
`path_is_captured()` is gated `!ctx->generated_only || ...`, and
`-c=native` sets `generated_only = false`, so that call is never even
reached on this path. The real cause: `ctx->typedefs`/`ctx->tags` are
whole-program registries (`vm->compiler.type_names` is one VM-wide list
across every command-line-input TU, never reset by the per-TU `#1001`
preprocessor reset), so `src/json.c`'s own file-scope
`typedef struct { Type **data; int len; int cap; } TypeVec;` — same name
and shape as `serialize_internal.h`'s, but an independent declaration with
no `#include` of its own — lands in that same registry as a
`from_include=false` record standing right next to the header's
`from_include=true` one. Two separate suppression gates
(`type_def_is_from_include_suppressed()`'s `ctx->defs` loop and
`typedef_alias_header_suppressed()`'s `ctx->typedefs` loop) each only ever
asked *the one record they'd resolved* whether it was header-supplied, so
whichever loop resolved json.c's own record won and reprinted the body.
Fixed by `typedef_name_is_header_supplied()`, shared by both gates: a
*tagless aggregate* record is also treated as header-supplied when some
other whole-program record of the exact same name is `from_include`.
Deliberately name-matched, not structure-matched (a structural match would
reopen the `#1168`/`#1169` trap — a same-representation scalar typedef
wrongly matching an unrelated header typedef), and deliberately restricted
to tagless aggregates only — a same-named *scalar* typedef collision (the
`#1057` `size_t`/`ptrdiff_t`/`wchar_t` demand-splice tests) is a real,
already-supported user redeclaration `has_colliding_user_typedef()` already
defers to, not a `#1290`-shaped hazard. Two residual gaps filed as
follow-ups rather than folded into this fix: **#1292** —
`path_is_captured()`/`emit_include_paths` still key off an
un-canonicalized path (a real hazard under `-c=generated` specifically,
where that function actually runs — confirmed both `cccc.h` spellings
replay side by side unmerged — though it turned out NOT to be #1290's own
cause); **#1293** — no rename pass exists for a *differently-shaped*
tagless typedef-name collision, only the same-shape one this fix covers
(`rename_colliding_type_tags()`'s own tag-collision analogue, #1014, has
no `ctx->typedefs` counterpart).

Re-running the full 82-file spike past the fix confirms both symptoms are
gone (`grep -c '} TypeVec;'` / `grep -c 'TY_VOID = 0'` on the raw `-m`
dump both `0`, and no "redefinition" diagnostic anywhere in a full
`-fsyntax-only` pass over that dump) and the spike proceeds well past
where it stopped — parse, link, serialize, and the perf-wall stats all
still clear at **17m54s**. It then hits **#1291**'s two round-12 findings:
(a) `src/internal.h`'s `extern volatile sig_atomic_t
_cccc_pending[...]`/`_cccc_any_pending`/`_cccc_sigev_pending`/
`_cccc_sigev_any_pending` (VM-managed signal-pending flags, genuinely
needs `volatile`) lost the qualifier when re-declared/re-defined in
`-c=native` output — both the auto-generated forward-declare-every-global
pass and the actual definition printed a plain, non-`volatile`
`sig_atomic_t`, a straight "redeclaration/redefinition with a different
type" against `internal.h`'s own real declaration; **RESOLVED**, landed
523a6c18. (b) a batch of libc declaration mismatches against the real
macOS SDK headers, split out as **#1294**, re-spiked once #1290/#1292/#1293
landed:

- `shm_open`'s fixed-arity `mode_t` parameter (`include/sys/mman.h`) vs. the
  macOS SDK's variadic `int shm_open(const char *, int, ...)` (POSIX and glibc
  keep the fixed 3-arg form; the bundled header now matches per-host), and
  `vprintf`/`vsprintf`/`vsnprintf`/`vfprintf`/`vscanf`/`vsscanf`/`vfscanf`'s
  missing `const`/`vsnprintf`'s `long`-vs-`size_t` size argument
  (`include/stdio.h`) — genuinely wrong C independent of the spike, **fixed**
  (#1295's own trunk landing) and confirmed gone from this re-run.
- `getchar_unlocked`/`putchar_unlocked`/`getc_unlocked`/`putc_unlocked` (the
  macOS SDK spells these as function-like macros, not plain declarations —
  `__stdoutp`/"too many arguments"/"redefinition ... as different kind of
  symbol") and `vsnprintf`/`snprintf`/`sprintf`'s checked-function `_chk`
  macro rewrite (`__builtin___vsnprintf_chk`/`__builtin___snprintf_chk`/
  `__builtin___sprintf_chk` "conflicting types") — genuine macro-shadowing,
  not a shape mismatch a header edit can fix. **Fixed** (#1294's own trunk
  landing, confirmed gone by isolated minimal repro — see
  `tests/test_native_libc_macro_shadow_1294.c`): a bodiless declaration's
  own declarator is now parenthesized (`int (getc_unlocked)(FILE
  *stream);`), which defeats the macro without giving up the prototype.
  Scoped to a declaration whose token traces to real source text — a
  comptime-synthesized declaration (`MakeFunction()`/`PublishNode()`) is
  left unparenthesized, since it has no real host declaration to ever
  collide with.
- `setpriority`/`getpriority` (`sys/resource.h` — CCCC's `int who` vs. the
  SDK's `id_t who`) and `catclose`/`catgets`/`catopen` (`nl_types.h` —
  CCCC's fallback prototype decomposed the `nl_catd` pointer typedef to
  `void *` instead of preserving the alias, vs. the SDK's real
  `nl_catd`-typed declaration) — filed separately as **#1296**, not part of
  #1294/#1295's scope. **Fixed** (same #1294/#1296 landing, confirmed gone
  by isolated minimal repro — see `tests/test_native_decl_alias_1296.c`):
  the header now spells `id_t who` directly; the fallback-prototype path
  now spells a from_include pointer typedef by its alias
  (`serialize_aliased_ptr_type_decl`, `src/serialize_type.c`) instead of
  decomposing it. This run's compile stopped at clang's default 20-error
  cap (`-ferror-limit`), so later mismatches past these five may still be
  hiding behind them — a future re-spike (not done as part of #1294/#1296)
  should raise the limit and would be the way to find out.

**Round 13.** Re-ran the full 82-file spike with `-ferror-limit=0` on the
host `clang -fsyntax-only` syntax-check of the dump, past #1294/#1296's
five diagnostics, to see what the default 20-error cap had been hiding.
Found one genuine, previously-undiscovered bug, general to any multi-TU
`-c=native`/VM compile (not self-hosting-specific — self-hosting is simply
the first thing to exercise it at scale): `declare_builtin_functions()`
(`src/parse_decl.c`) runs once per TU and reassigns the six setjmp-family
singleton fields (`vm->compiler.builtin_setjmp`/`_longjmp`/`_setjmp`/
`_longjmp`/`sigsetjmp`/`siglongjmp`) every time, so in a multi-TU program
only the last-parsed TU's `Obj` survives in each singleton. Both
`src/codegen_expr.c` (VM path) and `src/serialize_expr.c` (`-c=native`/
`-m` path) recognized these builtins via pointer identity against the
singleton, so every earlier TU's own setjmp/longjmp/sigsetjmp/siglongjmp
call silently fell through to ordinary call handling — confirmed with a
minimal two-TU repro that segfaults on the VM (`"invalid indirect call
target"`) and mis-serializes under `-c=native` (a call to literally
`setjmp` cast `(long *)`, never declared for the host, instead of the
correct `_setjmp((void *)...)`). **Fixed**: match these six reserved
builtins by name instead of singleton identity (new
`obj_is_reserved_builtin()`, `src/codegen_internal.h`, shared by both
files), plus `src/serialize_program.c`'s `serialize_synth_setjmp_decls()`
(same singleton-identity bug decided whether to emit the `extern _setjmp`/
etc. declarations at all). Regression: `tests/test_native_setjmp_multi_tu.c` (three TUs, VM 42 → native 42).

Investigating further along this same syntax-check found a second,
related class — `function_is_header_supplied()` (`src/serialize_program.c`)
requiring `obj->is_static` misses an external-linkage function *defined*
in a captured/replayed local `.c`-file inclusion (`src/vm.c`'s
`#include "ops.c"`) or a vendored single-header library
(`src/stdlib/format_printf.c`'s `stb_sprintf.h`) — both double-define once
self-hosting compiles the whole tree together. Two fix attempts (dropping
`is_static` outright; reading the function body's own token instead of the
declarator token) each broke other, unrelated tests — filed as **#1298**
rather than landed, with the concrete failure modes recorded there for
whoever picks it up.

**Round 13, full enumeration.** Re-ran the full 82-file spike with
`-ferror-limit=0` on the host syntax-check (previous rounds' clang default
20-error cap was hiding whatever came after #1294/#1296's five
diagnostics). cccc's own frontend still parses/links/serializes cleanly
(type-stats: `total=48475659 toplevel=17819220 ratio=2.7 max_depth=26`, no
separate wall). The syntax-check dropped from 91 errors (capped view,
pre-fix) to 48 (full view, post-fix):

- The entire 47-error setjmp/sigsetjmp "incompatible pointer types" class
  is gone — the fix above.
- 22 redefinition errors — exactly #1298's known residual, not new.
- 4 new `conflicting types for _setjmp`/`_longjmp`/`sigsetjmp`/`siglongjmp`
  errors, filed as **#1299**: the setjmp fix now correctly detects every
  TU's own call and emits `serialize_synth_setjmp_decls()`'s synthesized
  `void *`-typed extern declarations — which collide with `src/cccc.h`'s
  own genuine `#include <setjmp.h>` (real host use, for the *compiler's*
  own internal error recovery, unrelated to the guest-facing emulation).
  `cccc.h` is captured+replayed as real host source (needed so the host
  sees cccc's own real types), and once the host's own preprocessor opens
  that replayed text directly, `cccc.h`'s own `<setjmp.h>` resolves via
  the host's normal system search — outside CCCC's "always force the
  bundled copy" policy entirely, since that policy only governs CCCC's
  *own* frontend, not text a replayed `#include` hands the host to open
  itself. Confirmed via a direct `clang -E -H` trace: `math.h`/`setjmp.h`/
  `signal.h` appear as consecutive real SDK headers matching `cccc.h`'s
  own `#include` order exactly, and the literal text `#include <setjmp.h>`
  does not appear anywhere in the dump. Previously masked because the
  pre-fix `prog_calls_any()` singleton-identity bug meant the synthesized
  externs were never correctly emitted across this whole-program merge in
  the first place.
- 16 `incompatible pointer types assigning` (an anonymous struct pointer
  field captured from a real header re-cast to a freshly-reconstructed,
  structurally-identical-but-nominally-distinct anonymous struct at each
  `realloc()` assignment site) + 1 `called object type ... not a function`
  (a local hoisted to function scope by CCCC's own hoist-every-local
  strategy shadows a *later*, unrelated call to a global function of the
  identical name — `struct Node *expr;` shadowing a call to the real
  `expr()` parser function) + 1 `invalid operands` (an `offsetof()`-
  derived pointer-arithmetic expression gets a spurious `(char *)` cast on
  the integer offset operand, turning pointer-plus-integer into
  pointer-plus-pointer) — three distinct new classes, filed together as
  **#1300**.

Net: one real, general bug fixed outright; the spike's own error surface
is for the first time *fully* enumerated (no cap hiding anything further);
five known blockers remain (#1298, #1299, #1300's three), none
structurally related. The `include/cccc/pthread.h` CCCC-ABI polyfill
hazard this ticket names as a co-equal biggest risk still has not been
reached — every one of these is earlier in parse order than the
mutex/GIL-heavy files.

**Round 13, continued — a methodology bug, four fixes, one residual, one
new blocker filed.** Re-triaged #1298/#1299/#1300 in the same session.

First, a correction to the triage method itself: the syntax-check step's
own `clang -fsyntax-only` invocation used a bare `clang` resolved from
`$PATH`, which is *not* what `-c=native` actually invokes —
`cccc_find_native_cc()` (`src/vm.c`) searches `$PATH` for `cc` first. On
this machine that's a materially different, older compiler (Apple clang 17
via `/usr/bin/cc`) than plain `clang` (a much newer Homebrew LLVM 22, ahead
of it on `$PATH`). Confirmed directly: every one of #1300's 16 "incompatible
pointer types assigning" diagnostics (an anonymous struct pointer field
captured from a real header re-cast to a freshly-reconstructed, nominally
distinct anonymous struct at each `realloc()` assignment site) is a
*warning* under Apple clang 17 — `-c=native` compiles clean, exit 0 — and
only a hard error under the newer Homebrew clang the syntax-check happened
to reach first. Not a blocker; downgraded to a real but lower-priority
cleanup item, left open on #1300. **Lesson for future rounds: the
syntax-check must invoke `cc` (or whatever `cccc_find_native_cc()` would
resolve), never a bare `clang`.**

Re-ran the syntax-check correctly (`cc`, not `clang`) and confirmed/fixed
the rest:

- **#1299 (setjmp/cccc.h collision) — fixed.** `serialize_synth_setjmp_decls()`
  now skips its own void*-shaped `extern _setjmp`/`_longjmp`/`sigsetjmp`/
  `siglongjmp` declarations when a real `<setjmp.h>` is already reachable
  through some *other* captured header's own `#include` chain (`src/cccc.h`'s
  shape) — tracked by a new, narrowly-scoped flag
  (`vm->compiler.setjmp_h_reached_transitively`, set only by the `#1297`
  `ac_transitive_capture` arm, never the always-suppressed top-level one).
  The `__cccc_sigsetjmp` `#define` itself still needs emitting either way —
  it names no real libc symbol. Confirmed with a standalone minimal repro
  (any ordinary header containing `#include <setjmp.h>`, included from a TU
  that also uses setjmp/longjmp).
- **#1300, item 1 (offsetof pointer-arithmetic cast) — fixed.**
  `strip_casts()` (`src/serialize_expr.c`) peeled an *unbounded* chain of
  casts instead of exactly the one `usual_arith_conv()` itself adds — past
  that single legitimate layer it kept going, straight through a second,
  meaningful cast the guest source itself wrote (`(char *)an_integer_var`),
  misclassifying a genuinely pointer-typed operand as integer-ish. Changed
  the `while` to an `if`. Confirmed with a standalone minimal repro (any
  `(char *)an_int_var + offsetof(...)` pattern).
- **#1300, item 2 (hoisted local shadows a global function) — confirmed,
  split out as its own ticket, #1302.** Rediscovered independently before
  checking #1300 already covered it; built a minimal, standalone repro
  confirming the hypothesis exactly (a block-scoped local named the same as
  a sibling global function, hoisted to function scope by CCCC's own
  hoist-every-local strategy, shadowing an unrelated call to the global
  later in the same function). Not fixed this round — the fix needs a
  scope-aware rename of a hoisted local colliding with a global, a
  genuinely separate investment from items 1/3.
- **#1298 residual — fixed.** The external-linkage arm landed for #1298
  keyed its check on `obj->tok` alone, but `cc_link_progs` (#957) merges an
  external-linkage `Obj` across every TU that so much as *declares* it (a
  shared header's own bodiless prototype counts) — the merged `Obj`'s
  representative token can end up tracing to whichever TU's declaration the
  linker kept last, not the TU whose `#include` actually supplies the body.
  Exactly `src/internal.h`'s own relationship to `src/vm.c`'s `ops.c`-defined
  functions: `src/codegen_addr.c` and others reach several of them (
  `cccc_vm_heap_malloc` and siblings, `cc_type_shadow_*`,
  `cccc_call_native_function`, `cccc_current_ffi_vm`,
  `cccc_ffi_name_in_list`) purely through `internal.h`'s prototype, never
  re-including `ops.c` — 13 of the 82-file spike's original 16 ops.c
  redefinitions were this shape, only 3 were caught by the first fix. Fixed
  by also consulting `obj->body`'s own token (always traces to the real
  defining file) when `obj->tok`'s own file doesn't already qualify.
  Confirmed with a standalone minimal repro mirroring this exact shape.

Regenerated `src/std.c` along the way: `tools/generate_stdlib.c`'s
`get_std_header_name`/`get_stdlib_reg_fn_name` generators spelled a plain
`char *` return (and, for the latter, parameter) type where
`src/internal.h`'s own hand-written prototype spells `const char *` — a
pre-existing signature mismatch, invisible under the VM (which never
cross-checks a definition against a hand-written prototype the way a real C
compiler does), surfacing only once both texts reach the same host TU under
self-hosting ("conflicting types"). Fixed with `MakePointer(MakeConst(...))`.

Full 82-file re-run past all four fixes: the syntax-check (correct `cc`)
dropped from 32 real errors to **7**, and every one of those 7 is an
already-filed, already-known residual — nothing new: 6 redefinition
(`stb_sprintf.h`'s `STBSP__PUBLICDEF` family, `#1301`'s own shape-2
residual) + 1 `called object type ... not a function` (`#1302`, not yet
fixed). The spike's error surface for this round is now fully accounted
for by two open tickets, both already filed with concrete leads.
`include/cccc/pthread.h`'s own CCCC-ABI polyfill hazard still not reached.

**#1284 (candidate index over `ctx->seen`/`ctx->defs`) closed WONT_FIX.** It
was filed against the same perf wall this section documents, before the
`(a,b)` memo above was known to be the real fix; the memo removed the wall
directly (37.1M calls, 70% memo-served, wall 17m54s at full 82-file scale,
no separate hang visible since). The candidate-index approach it proposed
was tried once, under #1283 itself, and reverted — `ctx->seen`'s in-place
entry replacement (the #1010 incomplete→complete swap) and `ctx->defs`'s
`type_vec_remove_at` shifts defeated a dirty-flag-plus-rebuild invalidation
scheme (broke `tests/test_minilua.c` under `CCCC_TYPE_INDEX_VERIFY=1`). Any
future attempt needs identity-revalidation at probe time or a genuinely
shift/replace-aware incremental index, not a plain dirty flag.

The `include/cccc/pthread.h` CCCC-ABI polyfill hazard `#1132`'s body names
as the co-equal biggest known risk is still not reached — `host_signal.c`,
the `pthread_once` guards in `tokenize.c`/`parse_init.c`, and now the whole
POSIX-header-gap grind are past, but the mutex/GIL-heavy files further in
are not.

The frontend/serializer gaps found and fixed so far, in the order sessions
hit them:

- A `for (;;) { ...; goto DONE; } DONE: return ...;` shaped function
  (`src/build_shell.c`'s `read_word()`) false-failed cccc's own
  missing-return check for its struct return type — `statement_terminates`
  never recursed into a labeled statement. Fixed.
- `<string.h>` (`strtok`), `<glob.h>` (`GLOB_TILDE` and friends),
  `<sys/stat.h>` (`st_mtime` and friends), and `<stdio.h>`
  (`asprintf`/`vasprintf`) were all missing declarations cccc's own
  `src/build.c` uses. Fixed (see CHANGELOG).
- `<locale.h>` had no `__CCCC__`/`#include_next` hand-off (unlike
  `<pthread.h>`/`<fenv.h>`/`<stdio.h>`), so a real host compiler
  reprocessing it saw its own `struct lconv` collide with the real one.
  Fixed the same way (plus three bundled headers quote-including
  `"locale.h"` instead of `<locale.h>`, which would have bypassed the
  hand-off).
- The same `struct lconv` collision still reproduced at the full 82-file
  scale even after that fix — root-caused as `src/stdlib/ctype.c`'s own
  `#include <xlocale.h>` (macOS-only FFI registration for the `_l`
  family), which CCCC had no bundled counterpart for at all, so it fell
  through to the real SDK header. Fixed by adding a bundled
  `include/xlocale.h` (a plain alias for `locale.h`+`ctype.h`, portable on
  every host) plus a non-Darwin `-c=native` replay filter, since real
  glibc dropped its own `<xlocale.h>` in 2.26.
- `src/host_signal.c` uses `sigjmp_buf`/`sigsetjmp`/`siglongjmp` (its
  SIGSEGV/SIGBUS recovery guard) — cccc's own frontend had *no* support
  for this construct at all, unlike `setjmp`/`longjmp`/`_setjmp`/
  `_longjmp`. Fixed: `sigsetjmp`/`siglongjmp` are now hand-registered
  compiler builtins that alias the plain `SETJMP`/`LONGJMP` opcodes on the
  VM (the VM has no signal mask, so the `savemask` argument is evaluated
  for side effects then discarded), and lower to the real host
  `sigsetjmp`/`siglongjmp` under `-c=native` — `__sigsetjmp` on glibc,
  where `sigsetjmp` is a macro. `sigjmp_buf` is the same padded
  `long long[40]` shape as `jmp_buf`. Same change added
  `__attribute__((returns_twice))` to the synthesized `_setjmp`
  declaration and fixed a pre-existing VM codegen bug where a `setjmp`/
  `longjmp` argument containing a function call could clobber the env
  register mid-sequence and segfault.
- The `-c=native` output of `src/host_signal.c` needed `siginfo_t.si_addr`,
  `fileno`, and `sigprocmask` (with `SIG_BLOCK`/`SIG_UNBLOCK`/`SIG_SETMASK`),
  none of which the bundled `<signal.h>`/`<stdio.h>` carried. Added:
  `fileno` as a plain passthrough, `sigprocmask` as a translating wrapper
  (the guest `sigset_t` is a 4-byte bitmask, not the host's real one),
  `si_addr` overlaying the SIGCHLD union arm at its real per-platform offset
  (24 macOS / 16 glibc), `_Static_assert`-guarded.
- `#pragma once` (and `#ifndef` include-guard) suppression was keyed on the
  raw path *string*, so `src/macros.c` — which includes both `"./internal.h"`
  and `"./parse_internal.h"`, the latter's own `#include "./internal.h"`
  resolving against *its* directory to `src/././internal.h` — pulled
  `internal.h` in twice and redefined every `static inline` helper in it
  (`redefinition of cc_make_u64`). The key is now `realpath()`-canonicalized
  on every lookup/insert. This is a general `-c=native` completeness fix,
  not self-hosting-specific.
- `src/tokenize.c` / `src/parse_init.c` guard their lazy keyword/typename
  maps with `pthread_once`, which the bundled `<pthread.h>` did not declare
  (`error: expected ','` on `pthread_once_t`). Declared; then also
  FFI-registered for the VM (`wrap_pthread_once`, a CAS on the control
  object's state word plus a guest callback for the winner — the GIL gives
  it POSIX's block-until-init-completes semantics for free).
- The bundled-header POSIX gap batch: `src/main.c` calling `strcasecmp`
  with no `#include <strings.h>`; `strtok_r`/`strsignal` missing from
  `<string.h>`; `PATH_MAX` missing from `<limits.h>` (added as 4096, the
  larger of macOS 1024 / glibc 4096, with `<sys/param.h>`'s `MAXPATHLEN`
  now deriving from it); `realpath` missing from `<stdlib.h>`; `ctime_r`
  missing from `<time.h>`; `open_memstream` missing from `<stdio.h>`. All
  added as declarations + VM FFI registrations. Also fixed a pre-existing
  bug found alongside: `strtok` was declared in `<string.h>` but never
  registered, so a guest calling it compiled and then failed at run time.
- The `__cccc_*` accessor-name collision above was **not** a serializer bug
  as first suspected — the diagnostic fires at parse time
  (`src/parse_decl.c`), before the serializer ever runs, and cccc's check is
  correct C (a real host `clang`/`gcc` reject the same construct). The bug
  was cccc's own host-side implementation reusing the *guest*-facing
  `__cccc_*` name for its C identifier: `src/stdlib/posix_io.c` /
  `stdio.c` / `fenv.c`'s accessors (`errno`/`optarg`/`optind`/`opterr`/
  `optopt`/`environ`, `stdin`/`stdout`/`stderr`, `FLT_ROUNDS`) were
  `static`-defined under the exact same spelling the bundled headers declare
  `extern` for guest code. Beyond the linkage clash, reconciling the linkage
  alone would not have been enough: `errno` (and the others) macro-expand to
  `(*__cccc_errno_ptr())` even *inside* the accessor's own body under
  self-hosting, so a same-named definition would recurse into itself.
  Fixed by renaming the host-side C identifiers out of the guest namespace
  (`wrap_errno_ptr`, `wrap_stdin`, `wrap_flt_rounds`, etc. — the FFI
  registration string, which is what the guest actually sees, is unchanged),
  following the pattern `src/stdlib/stdlib.c`'s `wrap_mb_cur_max` already
  used.
- Round 8's batch, the same bundled-header POSIX-gap shape as every round
  above: `clock_gettime`/`clock_getres`/`clock_settime` (+ Linux-only
  `clock_nanosleep`) with `CLOCK_REALTIME`/`CLOCK_MONOTONIC`/etc. injected
  as host-derived `__CCCC_CLOCK_*__` macros rather than hand-transcribed
  (the same class of skew #779 fixed for errno codes); `kill`/`killpg`/
  `sigwait` and `pthread_sigmask`/`pthread_kill`; `SIZE_MAX`/
  `PTRDIFF_MIN`/`PTRDIFF_MAX` and the `SIG_ATOMIC_*`/`WCHAR_*`/`WINT_*`
  siblings; `strncasecmp` (`src/tokenize.c` missing `#include
  <strings.h>`); and `optreset` (BSD/Darwin only), following the same
  `wrap_*`-named accessor pattern `#1280`/`#1040` established for
  `optarg`/`optind`/`opterr`/`optopt`, including its own `-c=native` shim.
  `src/stdlib/pthread.c`'s `__atomic_compare_exchange_n` needed a
  different fix: cccc's frontend now recognizes the whole GCC/Clang
  `__atomic_*` builtin family, aliasing the `ND_ALOAD`/`ND_ASTORE`/
  `ND_EXCH`/`ND_CAS`/`ND_FENCE` nodes already reachable through
  `<stdatomic.h>`'s `__builtin_*` spellings.
- Past that batch, two more, smaller frontend gaps: `f().member` on a
  struct-returning function call (`gen_addr` had no `ND_FUNCALL` case,
  even though a struct/union return already has a caller-allocated
  `ret_buffer` and `gen_expr` already yields its address — `gen_addr` just
  needed to reuse that), and a single function with more labels than
  codegen's internal table allowed ("codegen: too many labels", raised
  256/1024 → 4096/16384).

Past all of that, the spike hits a genuine performance wall in
`same_type_or_origin()`'s multi-TU type dedup at whole-program scale — see
the status note above and the filed child ticket. No commitment yet to
stage (b) (full suite) or (c) (stage2 == stage3 reproducibility) — those
wait on (a) actually passing.

</details>
