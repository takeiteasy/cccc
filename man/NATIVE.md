# Native Compilation (`-c=native`)

How CCCC's serializer turns a compiled program back into portable C and
hands it to a real host compiler: the four output modes, what gets fully
lowered, what's deliberately left as GNU dialect, what's refused outright,
and the documented divergences from VM behaviour. See
[HEADERS.md](HEADERS.md) for how `#include` resolution and host-header
hand-off work under these modes, and
[TESTING.md](TESTING.md#native-round-trip-mode---native) for how the test
corpus exercises this path.

## The four output modes

CCCC's front end (preprocessor, parser, compile-time macro execution) is
shared by every mode below — they differ only in what happens *after* the
AST is fully resolved.

| Flag | What it does | Typical use |
|---|---|---|
| *(none)* | Compiles to VM bytecode, runs it in the built-in interpreter | Default: portability, sandboxing, the debugger, the safety suite, quick iteration with no system compiler |
| `-m` / `--dump-expanded` | Serializes the AST to portable C and prints it to stdout; does not compile | Inspecting what CCCC would hand to a host compiler; dialect round-tripping |
| `-c=generated` | Serializes to C and writes it to a file (`-o`, default `./a.c`); does not compile | Same serializer as `-c=native`, minus the final host-compiler step — for build systems that want to drive their own `cc` invocation |
| `-c=native` | Serializes to C, then hands the result to `CCCC_NATIVE_CC` (or `cc`/`clang`/`gcc`) and links a real executable (`-o`, default `./a.out`) | The production path: full toolchain performance, real system libraries, no VM overhead |

All three serializing modes (`-m`, `-c=generated`, `-c=native`) run the same
tree walk (`cc_serialize_program`, split across `src/serialize_program.c`,
`serialize_decl.c`, `serialize_expr.c`, `serialize_stmt.c`,
`serialize_type.c`, `serialize_shims.c`) and are subject to everything in
this document identically — "under `-c=native`" below means all three
unless stated otherwise. `--emit-cccc` is a dialect switch layered on top:
by default the serializer strips CCCC-only syntax down to portable C
(`@`-attribute shorthand, checked-pointer qualifiers, etc.); `--emit-cccc`
preserves that dialect instead, for round-tripping CCCC's own extended
syntax back through its own front end. `-c=native --emit-cccc` requires
`CCCC_NATIVE_CC` to point at a `cccc`-aware compiler, since a real host `cc`
cannot parse CCCC dialect. A separate flag, `--emit-only` (`-c=generated`
only), narrows output further to just the content explicitly routed with
`[[cccc::emit]]`/`@emit` — independent of `--emit-cccc`'s dialect choice.

`-c=native` unconditionally forwards a handful of flags a hand-driven
`-c=generated` build has to supply itself: `-lm`, `-pthread`, and
`-fsigned-char` (`run_native_backend`, `src/main.c`) — see "Output dialect"
below for why `-fsigned-char` in particular is required, not optional.

---

## `-c=native` scope

`-c=native`'s current scope is parity — the floor, not the ceiling. Any program that
already passes on the VM should produce correct native output too, on the
two currently-supported platform × arch combinations (macOS/Linux ×
aarch64/x86_64), using the real host's standard library wherever CCCC's own
header is a mere polyfill for VM-internal plumbing (auto-capture replays the
user's real `#include` verbatim, and a user `-I`/`-isystem` entry that also
holds CCCC's own bundled headers is demoted to `-idirafter` so the real
host header always wins the search, #1143 — see [HEADERS.md](HEADERS.md)),
and — wherever
CCCC's header instead encodes genuine VM-specific ABI with no host
equivalent (the fixed `is_compiler_owned_header` list: `stdarg.h`,
`setjmp.h`, `stdbool.h`, `stddef.h`, `stdint.h`, `inttypes.h`, `complex.h`,
`stdatomic.h`, `stdckdint.h`) — either a real translation to the host's own
ABI, or an explicit diagnosed rejection, never silent divergence. Blocks and
GNU vector `?:` meet the bar (lowered to portable C, not printed verbatim
assuming a specific host compiler); `asm(...)` is the one deliberate,
documented exception (always verbatim, since there's no VM equivalent to
translate to or from — see the table below). `atomic_flag` used to violate
the bar the other way around: spelled through the bundled typedef name,
which the host's own `<stdatomic.h>` defines as a *struct* (C11 7.17)
wherever CCCC's integer-flavoured header wasn't shadowing it on the include
path — every integer-style use failed to compile. Serialized output now
always spells the canonical `_Atomic _Bool` (#1109), which needs no header
and denotes exactly the type the VM modelled.

`--testing=native` (#1033) is the last piece of that batch: it doesn't
change this bar, but it's what finally exercises the entire
`tests/suites/` `[[cccc::test]]` corpus against it — see this file's own
`--testing=native` section in [TESTING.md](TESTING.md) for what it covers.

### The admissibility rule

A feature is admissible in serialized output iff it is **fully consumed
before `cc_serialize_program` runs** — i.e. by the time the serializer walks
the tree, nothing remains but ordinary typed AST nodes that already have
serializer support. This is enforced mechanically for both `NodeKind`s and
`TypeKind`s: `serialize_expr`'s and `serialize_type`'s `default:` arms
hard-error on any unhandled kind rather than silently emitting a comment or a
null statement (`src/serialize_expr.c` / `src/serialize_type.c`, #963c) — a
construct that reaches the serializer either has an explicit case or the
compile fails loudly, on the spot, naming the kind.

That guarantee does not extend mechanically to type *attributes and
qualifiers* — they are fields on a `Type`/`Obj`, not `NodeKind`s or
`TypeKind`s, so no `default:` arm can ever catch a dropped one; each has to
be audited by hand instead. The rule applied: is it a **hint** (no observable
effect if ignored — `always_inline`, `unused`, `deprecated`, `hot`/`cold`,
`noinline`), safe to drop silently; or a **contract** (changes layout,
linkage, placement, or semantics), which must be emitted, refused where no
emission is possible, or — for the narrow case below — documented as
unobservable, but never silently dropped without a reason on record.

`packed`, type-level `aligned(N)`, and a member's own explicit alignment
(`_Alignas(N)` or declarator-position `aligned(N)`, #1160) are contracts that
change layout and are now emitted (#1129) — `is_packed`/`align` were retained
on `Type`/`Member` but never re-emitted, so a native binary silently laid
such a struct out as if the attribute were absent; the emitted C was even
self-inconsistent (a `sizeof` folded against the VM's packed layout sitting
next to an unpacked struct definition). Fixed in
`serialize_aggregate_members`/`serialize_aggregate_attrs`
(`src/serialize_type.c`), shared by both aggregate-body emitters
(`serialize_struct_def` for tagged/typedef'd, `serialize_anon_aggregate` for
tagless). An object's own `_Alignas(N)` (`Obj.align`) was already covered by
`serialize_alignas_if_needed` (#1136).

`#1163` extended this to a packed aggregate's member alignment: outside
`packed`, `mem->align > mem->ty->align` is enough to tell an explicit
request apart from a member's own natural alignment, but under `packed`
that heuristic breaks (an explicit `aligned(4)` on an `int` resolves to the
same value as no request at all), so `Member.explicit_align` — 0 unless the
declarator carried its own alignment attribute — is used instead, and only
emitted when it exceeds 1 (an explicit request of 1 under `packed` is
already the layout default, and is also the only value guaranteed never to
violate C17 6.7.5p4's "never lowers alignment" rule for `_Alignas`).

`section`, `weak`, `visibility` are contracts CCCC's parser does not retain
at all — they hit the generic unknown-attribute path (`src/parse_types.c`)
and are dropped with a diagnostic (`warning: unknown attribute '...' ignored
[-Wattributes]`, included in `-Wall`), not silently. Deliberately left there
rather than retained-and-emitted: cccc is single-translation-unit with no
`dlopen`, so none of the three has an observable effect in either the VM or
a native binary compiled from one cccc invocation — a `weak` symbol has
nothing to be overridden by, `section` has no second linker input to place
relative to, and `visibility` has no shared-library boundary to control.
Emitting them anyway would manufacture a divergence in the *opposite*
direction — native honouring a weak override or section placement the VM
can never model — which is worse than the status quo. `#657`'s existing
`__has_attribute(visibility|section|weak)` == 1 pins the same "recognized,
architecturally inert" classification the format/alloc_size/etc. attributes
already get.

### Refusals: what `-c=native` declines to lower

The admissibility rule above is enforced by construction for `NodeKind`s and
`TypeKind`s, but a handful of shapes still reach an explicit, named
`error()`/`error_tok()` call in the serializer (`src/serialize_decl.c`,
`serialize_expr.c`, `serialize_stmt.c`, `serialize_type.c`,
`serialize_program.c` — the old single-file `src/serialize.c` was split into
these by #1150) rather than a `default:` case, because the *reason* they
can't be lowered is specific to the construct, not "unhandled". This table is
the complement of "any standard C compiles" — audited directly against every
`error`/`error_tok` call site in those five files (#1128).

| Construct | Site | Why |
|---|---|---|
| `__builtin_pc_function_name` / `__builtin_pc_source_location` | `serialize_expr.c` (`ND_FUNCALL` case) | resolves a VM bytecode offset through the VM's own symbol table; no host equivalent exists to translate to |
| bare reference to a nested function (`int (*p)(int) = inner;`, or passing one as a callback) | `serialize_program.c` (`collect_nested_refs`) | the lowered signature carries a hidden leading `__static_link` parameter no real function-pointer type can express (#1074). The one legal shape — a direct call's own callee — is exempted, not rejected |
| block literal at file scope | `serialize_expr.c` (`ND_BLOCK_LITERAL` case) | a block's descriptor is a *local*, living on the enclosing function's frame (#965); there is no frame at file scope to hold one |
| a block literal's own descriptor local, captured by a nested function's static link | `serialize_program.c` (`record_nested_upvar`) | a descriptor has no meaningful value to hand across a static link (distinct from an ordinary `__block`-storage local, which #1080 does support this way) |
| a direct call to a nested function whose own parent sits *beyond* a block ancestor of the caller (a sibling/cousin call reached only by climbing out of a block first) | `serialize_program.c` (`collect_nested_refs`) | needs the block's *enclosing frame*, which a heap-copyable descriptor deliberately never stores — the same invariant behind #1081's by-value-snapshot decision. Closed `WONT_FIX` (#1100; #1210 later filed as a duplicate). The VM rejects it identically (`codegen_expr.c`, `calling_nested` static-link walk), so this is not a native-only refusal. Distinct from the #1081-supported case, a *variable read* through a block ancestor, which reads the block's own capture snapshot |
| anonymous `struct`/`union` under `#pragma pack(N)` | `serialize_type.c` (`serialize_anon_aggregate`) | has no tag or typedef name to wrap in its own `#pragma pack(push, N)`/`pop` pair at file scope (#1173) — user-actionable: give it a name |
| a non-zero store into a member CCCC treats as host-owned layout (`{0}`-then-assign pattern reaching a `from_include` struct's member) | `serialize_expr.c` (`serialize_host_owned_zero_init_chain`) | the member path is CCCC's own projection, not necessarily the host's real layout — emitting the store would be silently unsound rather than merely incomplete (#1103) |

Every other `error`/`error_tok` site in those five files is an **internal-
invariant guard**, not a refusal of valid user input — reaching one means a
prior compiler pass violated its own contract (`serialize_stmt.c`'s
break/continue-target lookup; `serialize_expr.c`'s "block literal missing its
descriptor" and unexpanded-macro/splice checks; `serialize_type.c`'s
unresolved `TY_ERROR`/`TY_AUTO`; the `default:` arms in `serialize_expr.c`/
`serialize_type.c`, #963c; and `serialize_decl.c`'s relocation-resolution
checks backing #918/#925/#1044's fail-loudly initializer policy).

Two more refusals exist but are already tracked elsewhere, not new here:
`_Decimal`/`<decimal_math.h>` (`serialize_expr.c`/`serialize_program.c`, only
under a clang host — #1113, see the `_Decimal` section below) and
`_BitInt(N)` past 128 bits (`serialize_type.c`/`serialize_decl.c` — #1123,
open). `_Decimal`'s host-side refusal is also notable for *not* being an
`error()` call at all: `cc_serialize_program` emits an
`#if !defined(__DEC64_MAX__) #error ...` preamble into the generated C
itself, deferring the decision to whichever host compiler reads the output —
invisible to an `error(`-grep audit, worth remembering for the next one.

The remaining gaps below are plain ISO C the serializer could, in principle,
reconstruct but doesn't yet — accepted at the time, with no ticket carrying
them until this audit:

- ~~**`_Complex` as a global initializer**~~ — resolved (#1208): a
  `_Complex` constant expression folds at compile time (`eval_complex`) and
  `serialize_init_bytes` emits `__builtin_complex(re, im)` for a non-zero
  imaginary part. The generic final fallback still refuses any *other* type
  with no verified byte layout, by design.
- **#1074/#1209 residual: a fully multi-dimensional VLA (every extent
  runtime-sized, `int v[n][m]`) read by a nested function**
  (`serialize_program.c`, `record_nested_upvar`) — #1209 fixed a 1-D VLA
  local, a VLA with a fixed inner extent (`int v[n][3]`), and a
  pointer-to-VLA local with an initializer, by erasing the outermost extent
  of the env-struct field to an incomplete array (`T (*)[]`) and deferring
  the field assignment to the VLA's own in-place declaration site. A fully
  multi-dimensional VLA doesn't fit that fix — the field would need to be
  `int (*)[][m]`, a pointer to an array of incomplete element type, which
  is illegal C. #1221.

A further site, a bitfield initializer wider than 128 bits
(`serialize_decl.c`, `serialize_init_bytes`'s `TY_STRUCT` member loop), is
the same construct class as #1123 rather than a separate gap — tracked there
as a follow-up note, not its own ticket.

### Layout guards: `_Static_assert` next to every emitted aggregate

`-c=native`/`-m`/`-c=generated` const-fold `sizeof`, `_Alignof`, and member
offsets at parse time, then emit the aggregate definition alongside those
folded literals — correct only if the host compiler independently arrives at
the same layout CCCC did. Where it doesn't, the emitted C compiles clean,
runs, and writes out of bounds, with no diagnostic on either side. The native
round-trip corpus alone cannot see this: an affected test still passes, since
the runtime check it makes is against CCCC's own folded literal, not a value
the host actually recomputes.

To close that gap, every emitted `struct`/`union`/`enum` definition is now
followed by a `_Static_assert` layout guard: `sizeof`, `_Alignof`, and (for a
struct/union, one per named non-bit-field member) `__builtin_offsetof`. Any
disagreement between CCCC's own layout computation and the host compiler that
will actually consume the emitted C becomes a host compile error naming the
type, instead of a silent out-of-bounds write. On by default; `--no-layout-
guards` suppresses all of it.

```c
struct Point { int x, y; };
_Static_assert(sizeof(struct Point) == 8, "cccc/host layout disagreement: struct Point");
_Static_assert(_Alignof(struct Point) == 4, "cccc/host layout disagreement: struct Point");
_Static_assert(__builtin_offsetof(struct Point, x) == 0, "cccc/host layout disagreement: struct Point.x");
_Static_assert(__builtin_offsetof(struct Point, y) == 4, "cccc/host layout disagreement: struct Point.y");
```

**Two exclusions**, both required so the guard never fires on layout CCCC
never claimed to own:

- A type whose own layout is already deferred to the host
  (`type_layout_is_host_owned()`, `src/serialize_type.c`) — an ordinary
  `from_include` struct/union/enum, e.g. `struct timespec`.
- A type that transitively **contains** a compiler-owned-header type
  (`type_contains_compiler_owned_layout()`) — `va_list`/`jmp_buf` are
  deliberately widened to a safe upper bound covering every supported host
  (see `type_header_is_compiler_owned()`'s own comment), so CCCC's folded
  size for one is intentionally *not* the real host's; a guard there would
  fire on every host, gcc and clang alike, not just a divergent one. This is
  a deliberate deviation from a literal reading of the ticket that introduced
  these guards — reusing `type_layout_is_host_owned()` alone is not
  sufficient, since it returns `false` for exactly these types on purpose.

**Three documented residuals** — real gaps in coverage, not bugs:

- **Tagless aggregates.** A truly tagless, alias-less aggregate (no tag, no
  typedef, no `typedef struct {...} P;`-style anonymous-typedef alias) never
  gets a standalone definition emitted either — it's inlined at its point of
  use — so there is no name to write the assert with and no guard is
  attempted. `typedef struct {...} P;` **is** nameable and does get a guard;
  the residual is narrower than it might first appear.
- **`--emit-only` (`emit_strict`).** Under `--emit-only`,
  `type_def_is_from_include_suppressed()` returns `false` unconditionally
  (no auto-captured `#include` to defer to), which collapses both exclusions
  above to `false` too. Rather than emit a guard that might duplicate or
  conflict with one of CCCC's own bundled headers' pre-existing per-platform
  `_Static_assert`s (`include/sys/stat.h`, `signal.h`, `fts.h`, `aio.h`,
  `mqueue.h`, `ndbm.h`), no guards are emitted under `--emit-only` at all.
- **`--emit-cccc`.** This output is CCCC dialect fed back into CCCC's own
  front end, not a real host compiler — a layout guard would be tautological
  noise, so none is emitted.

**A genuine, permanent divergence, not a bug.** `#pragma pack(N)`
(#1173), `long double` (#1174), a too-narrow enum underlying type (#1175),
and an unnamed bit-field's contribution to struct/union alignment and size
(#1176, and its follow-up) were all real CCCC bugs, now fixed. One class of
divergence remains and is *intentional*: some host compilers, on some
targets, don't implement their own platform's real ABI rule for an unnamed
bit-field, in a way CCCC cannot match simultaneously with a target that
does —

- a width-0 unnamed bit-field's contribution to struct/union alignment
  (and, for a union, size) — `struct{char c; int : 0; char d;}` is 8/4
  under real AAPCS64 (Linux/aarch64, either compiler family) and 5/1
  everywhere else (x86_64, either compiler family; Darwin/arm64 clang);
- a nonzero-width unnamed bit-field's contribution to the same — `struct{
  char c; int : 3; char d;}` is 4/4 under real AAPCS64, 3/1 everywhere
  else; and
- an unnamed bit-field carrying an explicit `__attribute__((aligned(N)))`
  — `struct{char a; int : 5 __attribute__((aligned(16))); int c;}` is
  32/16 under real AAPCS64, 24/4 everywhere else.

This is the true AAPCS64 rule (`CCCC_ALIGN_ANON_BITFIELDS`,
`src/parse_types.c`), not a gcc-vs-clang split as originally believed —
verified directly against both compiler families, on x86_64, Linux/aarch64,
and macOS/arm64. **macOS/arm64 gcc-16 is the one outlier**: gcc never
implemented Apple's AAPCS64 deviation there, so it gives the AAPCS64 answer
on a target that doesn't use it — a permanent gcc/Darwin gap, not a CCCC
bug, matching the existing constructor/destructor-priority gap in the same
table. Since macOS's default `cc` is clang (which agrees with cccc's own
folded layout there), this divergence only surfaces as a **hard compile
error** naming the type under `CCCC_NATIVE_CC` pointed at a real Homebrew
gcc on macOS specifically — not under the default host, and not on Linux
under either compiler family. Escapes: `--no-layout-guards`, or simply use
the default host `cc`. The test corpus quarantines the handful of files
that instantiate one of these shapes via `NATIVE_SKIP_TESTS_GCC_MACOS`
(`tools/testing/__init__.py`) rather than softening the guard.

### Output dialect: GNU C11 required

The emitted C is **not** a fixed ISO standard — it requires a GCC/clang-
compatible host compiler. Within that requirement, the language-standard
floor is **C11**: `_Atomic`, `_Thread_local`, `_Static_assert`, `_Alignof`,
and `_Complex` are the newest ISO spellings ever emitted. Everything past C11
is already lowered rather than passed through — `nullptr_t` → `void *`
(#1111), `_BitInt(N)` → the smallest fixed integer container that holds it,
up to `__int128` (#1121/#1124), `atomic_flag` → `_Atomic _Bool` (#1109).

The GNU-extension axis, by contrast, is maximal and load-bearing — emitted
unconditionally, with no feature detection or fallback: `__attribute__
((vector_size(N)))`, `__attribute__((constructor/destructor[(prio)]))`,
`__int128`/`unsigned __int128`, `__extension__ ({ ... })` statement
expressions with `__typeof__`, `&&label`/`goto *(expr)`, `case A ... B` range
labels, `__asm__(...)` (verbatim — the one deliberate exception, since
there's no VM equivalent to translate to or from), and roughly two dozen
`__builtin_*` spellings including the entire `__atomic_*`/`__ATOMIC_*` family
the `<threads.h>` shim rests on. **`--std=` governs what cccc's parser
accepts, never what the serializer emits** — the native `-std=` flag
`run_native_backend` forwards is a *flag-spelling* probe
(`native_resolve_std_ladder`, `src/main.c:139-194`), confirming the host
accepts a given `-std=<ver>` string, not that it implements every construct
above. The probe always tries the `gnu<NN>` spelling of a rung before the
plain `c<NN>` one, regardless of which prefix the user typed (#1187) — see
the `-c=native` `-std=` forwarding section above for why a strict,
non-GNU spelling would otherwise reject constructs cccc's own frontend
only pedantic-warns on.

**Required host flags, beyond `-std=`.** `run_native_backend`
(`src/main.c`) unconditionally appends three flags a hand-compiled `-m`/
`-c=generated` build must supply manually: `-lm` (`:341`) and `-pthread`
(`:350`) for the math/threads shims, and `-fsigned-char` (`:363`, #1064) —
plain `char` is always signed under CCCC's own type rules, but a real host's
plain `char` is not universally signed (glibc/aarch64 defines
`__CHAR_UNSIGNED__`); its absence produces silently wrong answers on aarch64
Linux, not a compile failure.

### Future direction: a universal C lowerer (tracked as #1055)

A larger direction — CCCC as a genuinely portable C compiler that runs any
valid C (including its own supported extensions) on *any* host, POSIX or
not, polyfilling missing platform facilities rather than just targeting the
two platforms the VM itself already supports — is real and worth pursuing,
but is a different product bet (portability over parity) and a much bigger
undertaking. Deliberately not folded into the parity scope above — tracked
separately as #1055, not yet started.

## Serialized-output divergences

`-c=native`, `-m` and `-c=generated` re-emit the program as C and hand it to a
host compiler. The rule everywhere else in this document is that the emitted C
behaves as the VM behaves. Fifteen constructs cannot fully honour that, and
they are listed here rather than left to be discovered:

| Construct | VM | Serialized output |
|---|---|---|
| `asm(...)` | no-op by default; `--asm-passthru` compiles via native CC and executes through FFI | always emitted verbatim and executed by the host binary. There is no way to evaluate host assembly inside the VM — it would mean separately compiling each snippet and calling out to it, or parsing every host dialect into one uniform behaviour — so this is offloaded to the host by design, and the divergence is not mergeable from the VM side (#1119). Test-side consequence: inline-asm coverage lives in `tests/suites/test_suite_asm.c`, which is permanently skipped by the `--native` corpus (`NATIVE_SKIP_TESTS`); genuinely target-specific mnemonics inside it are arch-guarded with CCCC's predefined `__x86_64__`-style macros so they only exist where a real assembler accepts them |
| a zero-sized union passed through varargs (`printf("…", var[42])`) | the VM's own varargs machinery consumes no argument slot for a 0-byte aggregate; a `%d`-style conversion reading past it formats `0` | the host ABI passes the empty aggregate through a register and printf reads whatever is there — measured directly: clang `-std=gnu23` compiles it (empty unions are themselves a CCCC extension the host tolerates as GNU) and the conversion prints garbage (#1120). Not serializer-fixable: the emitted call site is verbatim guest source, and only the VM gives the argument defined semantics. Coverage lives in `tests/suites/test_suite_empty_union.c`, skipped by the native corpus entirely |
| `__builtin_return_address(n)` | a VM bytecode offset (`Pc`) cast to `void*` | a real host return address. Both are "the return address `n` frames up" in their own runtime; the numeric values are unrelated |
| `__builtin_dynamic_object_size(p, t)` | reads the VM allocation header, so the exact size is always known | the host builtin, which answers its documented "unknown" (`(size_t)-1` for types 0/1) unless the host optimizer can see the allocation — exact at `-O2`, unknown at `-O0`. Since #1159, the host's own optimization level is selectable with `-O<n>` (forwarded verbatim to the host cc under `-c=native`; the default with no `-O` on the command line is still the host's own `-O0`), so this divergence is now something a caller can steer around rather than something fixed at whatever level the host happened to default to |
| `__builtin_unreachable()` / `__builtin_trap()` / `__builtin_debugtrap()` | all three trap (one `BTRAP` opcode) | all three emit `__builtin_trap()`. The original spelling is not recoverable after lowering, and emitting `__builtin_unreachable()` would be undefined behaviour the host optimizer deletes — trapping is what matches the VM |
| `ioctl(fd, request, ...)` | `wrap_ioctl()` (#795) rejects any request code not on an explicit allowlist, regardless of `--posix-emulation` | the real host `ioctl()`, with no allowlist — any request code the host kernel itself accepts succeeds |
| `L` length modifier on an integer conversion (`%Ld`/`%Li`/`%Lu`/`%Lo`/`%Lx`/`%Ln`, `printf` and `scanf`) | the VM formatter follows glibc: `L` == `ll`, a full 64-bit slot — and `printf` `%Ln` writes a full 8-byte `long long` (#1228, `%Ln` in #1230) | the host libc decides. glibc agrees with the VM; BSD/Apple libc treats `L` as no-op on an integer conversion and reads/writes a 32-bit slot — `L` on `d` etc. is undefined behaviour in ISO C, so neither is "wrong". Not serializer-fixable: the emitted call site is verbatim guest source handed to the host's own `printf`/`scanf`. Coverage is `tests/test_printf_L_integer_modifier.c`, `CCCC_NATIVE_SKIP`. Tracked as #1231 (the libc-family axis has no policy knob, unlike #1206's `--bitfield-abi`) |
| `ppoll`; `sched_setparam`/`getparam`/`setscheduler`/`getscheduler`/`rr_get_interval` (`--posix-emulation`); `gethostbyname_r`/`gethostbyaddr_r`/`getnetbyname_r` (ungated) on a host with no real primitive (macOS for all of these) | the VM's own emulation/shim/stub (`src/stdlib/posix_poll.c`/`posix_sched.c`/`posix_net.c`) | `-c=native` (#1140) ports the same emulation/shim/stub into the emitted C via `serialize_posix_compat_shims` (`src/serialize_shims.c`), guarded `#if !defined(__linux__)` so a real host symbol is always preferred where one exists — parity, not a divergence, for the VM behaviour itself. `ppoll`'s emulation is not atomic (a signal between the mask swap and `poll()`'s wait is not guaranteed to interrupt it, same as the VM) — accepted, not fixed. CCCC's canonical constant numbering (`POLLWRNORM`/`POLLWRBAND`, `nl_item`, `LC_*`, `SCHED_*`) is now also translated to the host's real values under `-c=native`, for `poll()`/`ppoll()` and `nl_langinfo()`/`nl_langinfo_l()`/`setlocale()`/`newlocale()` alike, the same way the VM's own wrappers do — `rename_bundled_extern_for_native_shim`/`serialize_canonical_const_shims` (`src/serialize_shims.c`) renames the guest's declared-only reference and supplies a translating wrapper under the new name. The emitted `gethostbyname_r`/`gethostbyaddr_r`/`getnetbyname_r` mutex now also covers the plain `gethostbyname()`/`gethostbyaddr()`/`getnetbyname()` family (renamed and wrapped the same way, only when a program uses both families), matching the VM's own single shared mutex |
| `__builtin_alloca(n)` in a loop body that shares its block with a genuine VLA | each call gets its own address, live until the *frame* returns (`ALLOC_KIND_ALLOCA`), distinct from the VLA's own per-block storage (#981) | the host compiler's own stack-allocation lifetime, which is implementation-defined for multiple calls before the enclosing function returns and, confirmed directly (#1186), *compiler-family*-dependent, not just optimization-level-dependent: clang -O0 inserts a stacksave/stack-restore pair scoped to the VLA's block, so a bare `__builtin_alloca` call inside it gets the *same* address every iteration instead of a fresh one; gcc -O0 does not reuse the slot the same way, so this test passes there (`NATIVE_SKIP_TESTS_CLANG`, `tools/testing/__init__.py`) |
| Which -c=native host compiler `test_main_bad_argc_error.c`/`test_warning_main_bad_params.c` (bad `main()` signature warnings), `test_use_system_headers_pragma_suppress.c` (`#pragma clang assume_nonnull`), and `test_suite_decimal.c` (`_Decimal64`) round-trip under | n/a — VM-only constructs/warnings, no native equivalent to diverge from | clang treats a bad `main()` signature and gcc's `_Decimal64` gap as hard failures where gcc treats the first as a warning and supports `_Decimal64` natively; only clang recognizes `#pragma clang assume_nonnull` at all. Compiler-*family*-keyed, not GCC-version- or platform-keyed as first suspected (#1186) — `NATIVE_SKIP_TESTS_CLANG`/`NATIVE_SKIP_TESTS_GCC` (`tools/testing/__init__.py`), reproduce locally with `CCCC_NATIVE_CC=<compiler>` (see man/TESTING.md's "Native round-trip mode" section) |
| `__attribute__((constructor(N)))`/`(destructor(N))`'s priority argument, under gcc on Darwin specifically | codegen honours the numeric priority in the VM's own init/fini ordering | Darwin gcc (Homebrew, not Apple's `gcc`-is-actually-`clang` symlink) rejects the priority argument outright ("constructor priorities are not supported"); clang supports it and this round-trips there, as does gcc on Linux. Permanent, `WONT_FIX` gcc/Darwin gap (#1186) — `NATIVE_SKIP_TESTS_GCC_MACOS`, which needs both the platform and compiler-family axis since the group passes under gcc on Linux (`tools/testing/__init__.py`, split out from the platform-less `NATIVE_SKIP_TESTS_GCC` by #1193 after it wrongly suppressed the test there too). The identical gap in `tools/comptime_native_smoke.py`'s own case 114 (`case_ctor_dtor_native_round_trip`) is quarantined the same two-axis way via `SMOKE_CASE_SKIPS_GCC_MACOS` (#1196), that script's separate case-function-keyed skip table |
| a `void`-returning entry function that falls off its end | codegen unconditionally loads 0 into the return register at the end of the entry function, regardless of its declared return type, so the process always exits 0 (#1031) | no equivalent injection — the host compiler leaves a `void main`'s exit status undefined, so it is whatever the ABI happened to leave in the return register (observed values are not stable across hosts/compilers) |
| Tail-call elimination (`CALLT`, unconditional) | guaranteed: the VM reuses the caller's frame, so an arbitrarily deep tail-recursive call runs in constant stack space | best-effort, not guaranteed. The native build's TCO is the host cc's own heuristic, not part of any C standard. `-O<n>` is forwarded verbatim to the host cc (#1159); the deep-recursion tail-call tests pass `-O2` so both clang and gcc-16 eliminate the call (gcc's `-O1` heuristic does not, for some shapes). With no `-O` on the command line the host builds at its own `-O0` and does no TCO |
| `sizeof`/`_Alignof` of a `from_include` type, reached through a bitfield width (`int x : sizeof(struct statfs);`) or a global initializer's byte image (`serialize_init_bytes`) — including an array dimension on a struct/union *member*, or on an *initialized* global | folds against CCCC's own (correct-for-the-VM) type projection | stays folded. A bitfield width determines the *containing struct's own layout*, which CCCC also emits — re-materializing the width would make the host compute different member offsets than CCCC folded for the rest of that struct, actively unsound rather than merely incomplete, so this is not attempted (#1099, `WONT_FIX`). An initialized global's byte image is sized off the folded value by `serialize_init_bytes`; re-materializing only its declared dimension would desync the two (same reasoning, member arrays inherit it via the enclosing aggregate; also `WONT_FIX`). A bare `sizeof(T)`/`_Alignof(T)` *expression* (#1031), a *local or uninitialized-global* array dimension, a `case` label, and an enum value (#1095) all re-materialize the operator textually against the real host layout instead — see `man/HEADERS.md`. A `_Static_assert(sizeof(struct statfs) == N, "...")` condition depending on one of these types is a distinct case, not merely a re-materialization gap: CCCC's parser evaluates it against its own projection, so only a passing assertion ever reaches the serializer, which used to emit no `_Static_assert` construct at all — a host whose real layout would fail the same check compiled anyway. `-c=native` now re-emits the assert (both file- and block-scope forms) for the host to genuinely re-check it, gated on the condition actually depending on a host-owned `from_include` struct/union layout *and* the assert being written in a command-line input file — so one of CCCC's own bundled headers' own per-platform layout asserts (`include/sys/stat.h`, `signal.h`, `fts.h`, `aio.h`, etc.) is never re-emitted against the wrong host (#1098) |
`_Decimal32`/`_Decimal64`/`_Decimal128` declarations and literals (the `df`/ `dd`/`dl` suffix) pass through to `-m`/`-c=native` output as plain GNU decimal syntax — the only host-compiler-dependent construct in this document, rather than a fixed divergence or a fixed hard error. gcc implements the GNU decimal extension (confirmed on both macOS `gcc-16` and Linux gcc 15.2, both predefining `__DEC64_MAX__`) and the output compiles and runs correctly there; clang implements none of it, on either platform, and rejects the syntax outright ("GNU decimal type extension not supported") — see the compiler-family row above (`test_suite_decimal.c`, `NATIVE_SKIP_TESTS_CLANG`). Since a real passthrough path exists on gcc, the serializer cannot hard-refuse unconditionally without regressing that working configuration; instead it emits a guarded `#error` preamble (`serialize_decimal_native_guard()`, src/serialize_program.c`) the moment any decimal type is used anywhere in the program, deferring the actual refuse-or-not decision to whichever host compiler reads the output (`#if !defined(__DEC64_MAX__)`) — giving clang a cccc-branded diagnostic instead of its own confusing one, with no effect on gcc (#1113).

Two decimal constructs remain genuine hard errors, independent of host
compiler, because no host — gcc included — has an equivalent to lower to:
`__builtin_decimal_to_chars` (`src/serialize_expr.c`) and `#include
<decimal_math.h>`'s replay (`src/serialize_program.c`). A `CCCC_HAS_DECIMAL=0`
build refuses to serialize any decimal construct at all, for the same reason.

A variable-length array declared in a `for`-loop initializer
(`for (int i = 0, v[n]; ...)`) is likewise a hard error under `-m`/
`-c=native`: the VM runs it fine, but the init clause is serialized as
comma-joined assignments and C forbids mixing a declaration with expressions
there, so it is rejected with a diagnostic rather than emitted as broken C
(#964).

`_BitInt(N)` with `N` in `(64, 128]` — including `__int128`/`__int128_t`/
`__uint128_t` (sugar for `_BitInt(128)`, see the GNU Extensions table above) —
serializes as the real host `__int128`/`unsigned __int128` under `-c=native`/
`-m`/`-c=generated`; supported by clang and gcc on every host this project
targets. `_BitInt(N)` with `N > 128` is a hard error there instead: those
backends have no multi-word lowering (only the VM's own address-based
`wide_bitint.c` path handles arbitrary widths up to `BITINT_MAXWIDTH`), so
rather than silently truncate into a container too narrow to hold the value —
the #1121 bug this replaced, where `_BitInt(65..128)` fell into the same
64-bit `long` arm as `_BitInt(N<=64)` and every operation beyond 64 bits was
silently wrong — an out-of-range width now fails loudly at the type-emission
site (`serialize_type`'s `TY_BITINT` case, `src/serialize_type.c`), matching the
project's general fail-loudly policy for what these backends cannot represent
(#824, #1121). This is also why `tests/suites/test_suite_c23.c` (which uses
`_BitInt(256)`/`_BitInt(4096)`) stays off the native corpus (`NATIVE_SKIP_TESTS`)
even once its unrelated `_Decimal` blocker (#1104) is cleared.

`_BitInt(N)` with `N` in `(64, 128]` is aligned 16 on every target (see the
`_BitInt(N)` row above), matching the `__int128` host container it always
lowers to under `-c=native`/`-m` — but clang's/gcc's own *native*
`_BitInt(65..128)` (the spelling, not `__int128`) is align 8 on x86_64 and
align 16 on aarch64; they are not self-consistent with each other across
targets. cccc picks the rule that keeps its own parse-time `sizeof`/`_Alignof`
matching the layout of the C it itself emits on all four supported targets,
rather than chasing a target-dependent host rule — so a struct or expression
mixing a cccc-compiled `_BitInt(65..128)` object with a real clang-compiled
one on x86_64 must not assume they share layout for that spelling. `__int128`
itself has no such divergence: it is align 16 in clang/gcc on every target,
matching cccc (#1135).

Until #1122, none of the above was actually reachable for a *global
initializer*: any file-scope object whose type needed writing more than 8
bytes at once — `_BitInt(N)` for `N > 64`, `__int128`, `long double`, or
`_Complex` — crashed at parse time (`write_gvar_data`'s scalar tail only
handled 1/2/4/8-byte writes) under the plain VM as well as under
`-c=native`/`-m`. That crash, not any width limit, is what made
`serialize_init_bytes`'s `_BitInt(N)` arm (added by #1121, above) dead code
in practice. #1122 fixed the underlying compile-time constant folder (now
arbitrary-width, reusing the VM's own `wide_bitint.c` runtime helpers so a
global folds to the same bytes a local would compute) and added `long
double`/`_Complex` arms alongside it. The result is exactly the divergence
already documented in this section, now actually reachable: a `_BitInt(N)`
global with `N` in `(64, 128]` (including `__int128`) serializes under
`-c=native`/`-m`; one with `N > 128` works under the VM and is refused at
type-emission under `-c=native`/`-m`, same as any other `_BitInt(N > 128)`
use. This compiler still has no imaginary-literal syntax (`3.5i` is a parse
error), but since #1208 `I` / `_Complex_I` / `CMPLX()` — which desugar to an
`__cccc_cmplx(...)` node — and `+`/`-`/`*`/`/`/unary-`-`/`conj` over complex
constants fold at compile time via `eval_complex` (`src/parse_expr.c`), whose
arithmetic is bit-for-bit identical to `gen_complex_expr`
(`src/codegen_addr.c`, the VM's own runtime complex path) so a global
initializer and the same expression in a local agree. `serialize_init_bytes`'s
`TY_COMPLEX` arm emits `__builtin_complex((elem)re, (elem)im)` when the
imaginary half is non-zero (both clang and gcc accept that in a static
initializer) and keeps the bare real literal — byte-identical to pre-#1208
output — when it is exactly zero. `creal`/`cimag` also fold in an ordinary
real constant-expression context (`static double d = creal(I);`,
`_Static_assert`). Found and fixed alongside #1208: a `float -> _Complex`
cast (the real operand of a construction like `1.5f + 2.5f*I`) was routed
through the real→integer saturating helper under `-c=native`/`-m` and
truncated `1.5f` to `1`, because `is_flonum()` is false for `TY_COMPLEX`;
`serialize_expr`'s `f2i_native` gate now excludes a `TY_COMPLEX` destination.

Found in the same #1121 audit but out of its scope, both now resolved:
`serialize_type`'s container-by-`size` mapping above picks the *smallest*
standard container that fits `N` bits (e.g. `_BitInt(5)` → a plain `char`),
which naturally over-shoots `N` for any width that isn't itself exactly
8/16/32/64/128 — and nothing re-masked a computed value back down to `N`
bits the way the VM's own `emit_bitint_trunc` (`src/codegen_emit.c`) does,
so e.g. `unsigned _BitInt(5) u = 31; u = u + 1;` gave `0` under the VM but
`32` under `-c=native`/`-m` (#1124). Fixed by wrapping every arithmetic/
cast/unary result of such a type in an explicit shift-pair mask
(`serialize_expr`'s `bitint_needs_mask`/`bitint_op_needs_mask` gate,
`src/serialize_expr.c`) — computed in a fixed-width intermediate (`long`/
`unsigned long`, or `__int128` for a `(64,128]` container) rather than the
container type itself, since C's integer promotions would otherwise re-widen
a `char`/`short` container back to `int` for the shift regardless of a
preceding cast to it; the left shift specifically runs on the *unsigned*
variant of that intermediate even for a signed `_BitInt`, since shifting a
negative signed value left is only well-defined starting C23 and clang warns
on it (`-Wshift-negative-value`) regardless of the output file's own
`-std=`. A width exactly matching its container (8/16/32/64/128) emits no
mask, since the container already truncates for free there.

Separately, a bitfield whose declared type is itself a wide `_BitInt`
(`T f : W;`, `T`'s width over 64 bits) used to crash the VM outright:
`emit_load_ex`'s wide-`_BitInt` arm hands back the storage unit's *address*
unchanged (wide values are address-based everywhere else in this compiler),
and the ordinary scalar bitfield shift/mask code then ran on that address as
if it were a value, corrupting it before any consumer dereferenced it
(#1125). The struct/union/wide-`_BitInt` assignment fast path
(`src/codegen_expr.c`'s `ND_ASSIGN`) had the mirror problem on the write
side: a bitfield's assignment type is its *container* type, so it took the
whole-container `MCPY` path, which ignores `bit_offset` entirely and can
write past the struct (bit *packing* within a struct is laid out compactly
regardless of container width — `struct W { _BitInt(256) f : 193; }` packs
`f` into bits `[0,193)`, though its container spans 32 bytes; since #1127
the *struct itself* is 32 bytes here (not 25) because a named bitfield
member's declared type also sets the struct's own size/alignment floor to
its storage unit — see the Bitfields row under [C89 / C90](#c89--c90)
above).
Fixed with two new runtime helpers, `__cccc_bitfield_extract`/
`__cccc_bitfield_insert` (`src/stdlib/wide_bitint.c`), that walk only the
exact bytes the field spans rather than assuming a whole container is
present or fits a register; the parse-time global-initializer RMW
(`write_gvar_data`, `src/parse_init.c`) now shares the same `insert` helper
instead of its own word-array loop, which had an analogous past-the-object
overwrite. A wide-`_BitInt`-typed bitfield's ordinary runtime read/write is
correct under `-c=native`/`-m` too, since the emitted C just spells it as a
real bitfield of that width and lets the host compiler handle storage.
A **global initializer** for such a bitfield used to diverge there:
`serialize_init_bytes`'s own bitfield-value re-extraction clamped its read
to 8 bytes and printed a plain `%llu` literal, silently dropping any bit at
or above bit 64 of the field's value (#1126, found while adding
native-corpus coverage for #1125, not fixed by it at the time). Resolved by
replacing that clamp with a byte-granular extract over the field's exact
`[bit_offset, bit_offset+bit_width)` span — the same shape
`__cccc_bitfield_extract` above uses — plus sign extension and a 128-bit hex
literal for any value that doesn't fit `long long`/`unsigned long long`
(dodging the "sign-extended `INT64_MIN` isn't a valid `long long` constant"
trap at `bit_width >= 64`). `_BitInt(N<=128)` bitfield globals now
round-trip cleanly through `-c=native`/`-m`; `N>128` still hits
`serialize_type`'s own container refusal (#1123, no multi-word lowering
exists there yet). Covered by `tests/test_wide_bitfield_global_init_1126.c`
and, now that the suite is back on the native corpus, by
`tests/suites/test_suite_typesystem.c`'s own `test_wide_global_init` (case
12, from #1122) and `test_wide_bitfield_global_offset`.

A function-local `static` array initialized with computed-goto label
addresses (`static const void *disptab[] = { &&L0, &&L1 };`, the usual
dispatch-table idiom for a `goto *disptab[i]` interpreter loop) serializes
correctly under `-c=native` — unlike the `for`-loop VLA case above, GNU
labels-as-values in a `static` initializer is not a divergence forced by
C's grammar: both GCC (documented in its manual) and clang accept exactly
this construct at function scope. `-c=native` ordinarily hoists a
function-local `static` out to file scope as a synthetic global
(`__cccc_disptab_N`), where `&&L0` has no C spelling at all — so instead,
whenever a `static`'s initializer relocation resolves against a label
rather than another global, its real definition is deferred and emitted
inside the one function that owns the label
(`collect_deferred_static_labels()`, `src/serialize_program.c`), the only place the
address is legal to spell. A candidate referenced from more than one
function (a block literal or nested function lexically inside the owner,
which `-c=native` lifts to its own separate file-scope C function) is left
undeferred, and still hits the diagnostic below rather than emitting
broken C — the general fail-loudly policy this file follows throughout:
"cannot serialize initializer for global '...' in native mode: unresolved
relocation target" (#1044).

Three general `-c=native` serializer gaps found auditing `tests/test_minilua.c`
(#1042) are fixed, not divergences: (1) a struct/union first reached only
through a *pointer* reference (e.g. a function-pointer typedef parameter)
could still print its *by-value* user's definition first, "field has
incomplete type" — a stable topological reorder pass over the collected type
list now moves a type's own body ahead of any by-value user, without
disturbing pairs whose order was already legal. Relatedly, a struct/union
tag named only inside a function-pointer parameter's own *prototype scope*
(C11 6.2.1p4) is now forward-declared at file scope ahead of the type-def
block, so it resolves against the same tag as the type's own later,
file-scope definition instead of manufacturing an "incompatible function
pointer types" error between two identically-spelled but distinct types.
(2) `offsetof(T, member)` is a genuine integer constant expression (C11
6.6p9), but was misclassified as a variable-length array — a live VM bug
independent of `-c=native` (`sizeof` of the containing aggregate was simply
wrong), on top of the `-c=native` symptom (the VLA-length replay path had no
dependency tracking for a type named only inside the length expression,
"use of undeclared identifier"). (3) an `#include` auto-captured from the
user's own source can legally appear, in the source, *after* a `static`
declaration whose name happens to match a real host libc symbol — legal C,
since a later, weaker declaration of an already-defined `static` doesn't
redefine it — but the include-replay block hoists every captured `#include`
to the very top of the emitted C, unconditionally, manufacturing a
collision the user's program never actually has. `rename_colliding_static_
names()` now probes the host libc's own symbol namespace (via `dlsym` on
the same handle the VM's own FFI loader uses, never `RTLD_DEFAULT`/
`dlopen(NULL)`, which would also see the compiler process itself) and
renames a colliding `static` the same way it already renames a cross-TU
collision.

Three more serializer bugs found by #1033's native-corpus sweep of
`tests/suites/` are likewise fixed, not divergences (#1102): (1) `&` over
a block-scope compound literal — lowered to `ND_ADDR` over a comma chain of
memzero + assignments + hidden temp — used to spell the `&` over the whole
chain, but C's comma operator never yields an lvalue, so every host
compiler rejected the output; it now binds to the chain's addressable tail
(`(memset(...), t.x = 30, &t)`) — including under a postfix shell above the
chain (`&((struct P){40, 41}).x`, or `->y` through a pointer-typed
literal, whose `->` lowers to an explicit deref): `(..., &t.x)` /
`(..., &(*t).y)`. (2) `-(-5)` — a macro-expanded
double negation, possibly behind an implicit widening cast that serializes
as nothing (`_Generic`-selected arms) — spelled flat as `--5` and re-lexed
by the host as pre-decrement; the inner negation keeps its parentheses.
(3) a const-element aggregate local (`const int a[3] = {1,2,3}`) hoists
declaration and initializer apart like any local (#1029), but the
qualifier lives on the *element* type one level below where #1029's strip
looked, leaving a genuinely-const object for the per-element assignments
to store into — clang rejects any statically-const store even through an
explicitly-cast-away pointer ("read-only variable is not assignable"), so
both the hoisted declarator and the byte-offset cast-back now drop the
element qualifier. A fourth spelling from the same sweep is likewise fixed
(#1111): an implicit conversion into C23 `nullptr_t` — assignment
conversion, or null-pointer-constant equalization in a comparison — printed
its cast destination as the bundled `<stddef.h>` typedef name,
`np = (nullptr_t)0;`, but casting *to* `nullptr_t` is not valid C23 syntax
even where assignment/conversion would be, and every host compiler rejects
it outright. Cast destinations now spell `(void *)`: the host `nullptr_t`
is `typeof(nullptr) == void *`, same size and representation, so every
assignment and comparison keeps its exact meaning. Declarations of
`nullptr_t` objects keep their typedef name (valid C23), so only cast
sites rewrite.

`__builtin_pc_function_name(pc)` and `__builtin_pc_source_location(pc, &file,
&line)` are also a hard error under `-m`/`-c=native`/`-c=generated`, rather
than a divergence — the opposite direction from the `__builtin_return_address`
row above, even though the two are documented to compose. Both lower to a call
into a VM-only FFI shim (`__cccc_pc_to_name` / `__cccc_pc_to_source`,
registered by `cc_load_symbolize_runtime`) that resolves a VM bytecode offset
through the VM's own symbol/source-map table; neither the shim nor the table
it reads exists natively, so — unlike `__builtin_return_address`, which maps
faithfully to a real host return address — there is no meaningful native
behaviour to fall back to. Rejected with a diagnostic naming the builtin
(#969).

A genuine GNU nested function (a function defined inside another function's
body, not an Apple block literal) serializes by lowering to a plain C
function plus an explicit environment struct, the same shape blocks already
use (#965/#1074): every function that directly parents a nested function gets
a `struct __cccc_nenv_<name> { void *__up; T0 *__uv0; ... }` at file scope
(one pointer field per enclosing local/param any of its nested descendants,
at any depth, actually reads or writes — `serialize_nested_preamble()`,
`src/serialize_program.c`), an instance (`__cccc_nenv`) declared and populated at the
top of its own body, and every direct call to a nested function passes the
right env pointer as its (already-parser-synthesized) `void *__static_link`
first parameter — its own env for a direct child, or a chase through
`->__up` (mirroring `codegen_expr.c`'s `calling_nested` static-link walk
exactly) for a sibling or an ancestor's nested function. An outer
local/param reference from inside a nested body is rewritten to
`(*env->__uvK)` instead of the bare (otherwise out-of-scope-at-file-scope)
identifier.

A VLA local, or a pointer-to-VLA local whose own declarator reads a runtime
variable, read by a nested function IS supported (#1209) — its declaration
can't be hoisted ahead of the point that would need `&var` (#964), so
instead the env-struct field's outermost VLA extent is erased to an
incomplete array (`T (*)[]`, the same spelling C already uses for a
flexible array member) and the field is assigned at the VLA's own in-place
declaration site, once `&var` is finally valid, rather than at the top of
the function with every other upvar. `serialize_nested_upvar_ref()`'s
`(*env->__uvK)` rewrite needed no change — the erased pointer dereferences
and decays exactly like the original. Only a fully multi-dimensional VLA
(every extent runtime-sized, `int v[n][m]`) doesn't fit this: the field
would need to be `int (*)[][m]`, a pointer to an array of incomplete
element type, illegal C — still rejected with a diagnostic naming the
construct rather than serialized wrong (#1221).

One shape has no portable lowering and is rejected with a diagnostic
naming the construct instead, per this file's own "explicit diagnosed
rejection, never silent divergence" rule (a second, distinct shape
involving a block ancestor is rejected too — see #1100 below): any bare
reference to a nested function's own value that ISN'T the direct callee of
a call to it (e.g. `int (*fp)(int) = inner;`, or passing `inner` as a
callback) — the hoisted signature's extra leading `__static_link` parameter
has no portable function-pointer type. A `__block`-storage local captured
by a nested function IS supported: its own C storage is already a pointer
(the shared heap box), so the env field holding its address is one level
of indirection deeper (`T **` instead of the ordinary upvar's `T *`) and
every read/write goes through an extra dereference (#1080/#1081, below).

Both nesting orders of "a block literal and a genuinely nested function,
one directly inside the other" are fully supported on both back ends,
closed this batch:

- A block literal defined *inside* a nested function, capturing a variable
  owned by that function's *own* ancestor (not the nested function's own
  local): the VM itself silently miscompiled this shape until #1076 fixed
  it (a parse-time capture-collection gap plus a missing codegen source
  arm — see that ticket's own resolution comment); `-c=native` rejected it
  outright until **#1080** gave it a real lowering — `collect_nested_refs()`
  registers the ancestor-owned capture as an upvar of its real owner
  (`record_nested_upvar()`) instead of rejecting it, and `ND_BLOCK_LITERAL`'s
  own capture-copy loop reads it back through the same env chase
  (`nested_env_ptr_expr()`) an ordinary nested-function upvar reference
  uses.
- A nested function defined *inside* a block literal, reading a variable
  owned by the block's own enclosing function (the opposite nesting order)
  — **#1081** — was broken on BOTH back ends independently: a block's own
  `__static_link` slot holds its descriptor pointer, not a plain frame base
  pointer, which broke the VM's uniform multi-hop static-link chase
  (`emit_static_chain_var_addr`, `src/codegen_addr.c`) the moment it needed
  to hop *through* a block ancestor (a single hop, reading the block's own
  local/param directly, was already correct and is unaffected); `-c=native`
  independently misapplied its nested-function-upvar machinery
  (`NestedEnvEntry`) to a block ancestor as if it were a real nested
  function's env, chasing the block's real `__static_link` (its descriptor
  pointer) as another such env — compiling clean and segfaulting at
  runtime. Fixed on both back ends by detecting the nearest block ancestor
  on the chase and terminating there: the VM reads the variable out of that
  block's own capture descriptor instead of continuing to hop through it as
  a frame pointer, and `-c=native` reads it out of the block's real
  descriptor the same way (`block_ancestor_desc_ptr_expr()`,
  `src/serialize_expr.c`). This requires the variable to actually be captured by
  that block — `block_literal()`'s transitive-capture climb now also walks
  every nested function defined directly inside a block's own body
  (`Obj.nested_children`, recorded by `parse_decl.c`), so a variable
  referenced only inside such a nested function still ends up in the
  block's own captures list. **Design decision:** the nested function sees
  the block's own creation-time snapshot of an ancestor-owned variable —
  exactly like a sibling direct block read already does — not a live read
  of the ancestor's frame; there is no reference implementation to defer to
  for this exact combination (clang has blocks but no nested functions,
  gcc the reverse), so internal consistency with the block's own direct
  captures is the spec. Write-propagation still requires `__block`, the
  same rule blocks already have.

A third, structurally similar shape is a distinct, permanent gap —
**#1100**, closed `WONT_FIX` — found while fixing #1081: calling a nested
function whose own parent sits *beyond* a block ancestor (a sibling/cousin
call reached only by climbing out of a block first, not a plain variable
read) needs the block's own *enclosing frame*, which a heap-copyable
block's descriptor deliberately never stores — by design, the same reason
#1081's own snapshot decision above was made. Confirmed broken on both
back ends pre-fix (VM: wrong answer; `-c=native`: compiles clean,
segfaults at runtime) and now rejected with a diagnostic ("calling a
nested function whose parent is beyond a block ancestor is not supported
(#1081 residual)") on both — `codegen_expr.c`'s `calling_nested`
static-link walk (VM) and `src/serialize_program.c`'s `collect_nested_refs()` (native,
checked ahead of `nested_env_ptr_expr()`'s own call-site rewrite) — rather
than left to miscompile silently. A real fix would need a fundamentally
different mechanism (e.g. passing the block's own creating frame's address
down through the descriptor for the duration of a synchronous nested call
only, never stored past it, with escape analysis to guarantee the block
never outlives that frame) — significant new scope and safety machinery
for a shape with no known real-world occurrence, so this stays a
diagnosed rejection rather than a miscompile, same disposition as #1099
above. #1210 was later filed against this same shape (the #1128
native-refusal audit did not spot #1100) and closed as a duplicate; the
rejection on both back ends is now pinned by
`tests/test_nested_fn_call_beyond_block_1100.c`.

A nested (non-`static`) function *definition* whose name matches an
enclosing file-scope function is supported (#1075): C17 6.2.1p4 treats scope
and linkage as separate axes, so the block-scope definition introduces a
distinct function rather than "redefining" the outer one — the parser gives
it its own `Obj` (the same lookup already used for an explicit `static`
nested definition), leaving the outer function's own callers, and any
bodyless block-scope *prototype* of it elsewhere (which does still bind to
the outer function, per #1056's own guarantee — a declaration with no
storage-class specifier has external linkage), unaffected. Serialization
hoists the nested definition to file scope under its original name like any
other nested function, so `rename_colliding_static_names()` (`src/serialize_program.c`)
now also treats a non-static defining `Obj`'s name as a fixed anchor that a
same-named nested (or otherwise `static`) `Obj` always yields to, renaming the
nested copy rather than colliding with it.

Blocks `^{ ... }` serialize by lowering to a plain C function plus an explicit
environment struct (#965) — not by emitting `^{ }` verbatim, so no
`-fblocks`/libBlocksRuntime dependency is introduced. A block value becomes a
pointer to `struct __cccc_block` (`{ void *__invoke; long __size; }`); each
block literal gets a paired `struct __cccc_block_env_N` (the same two fields
plus one per capture, in descriptor-slot order — a `__block` capture's field
is a pointer to the captured type, matching the VM's own shared heap box) and
is built as a comma expression writing into that struct; a block call becomes
a GNU statement expression that loads `->__invoke` and calls it with the
descriptor as the first ("static link") argument. A `__block` local is
declared as a pointer and `__builtin_malloc`'d at function entry, exactly
mirroring the VM's own `ALCB` prologue allocation — including never being
freed, so it leaks in serialized output the same way the VM's own
`ALLOC_KIND_BLOCK_BOX` is never reclaimed (see [SAFETY.md § Individual
Memory Safety Features](SAFETY.md#individual-memory-safety-features)).
`Block_copy` gets a
native replacement for its VM-only FFI shim (`__cccc_block_copy_impl`,
emitted only when `Block_copy` is actually reachable); `Block_release`
already lowers to a plain `free()` call and needs no special handling when a
`free` prototype is already in scope (`<stdlib.h>` `#include`d). When it
isn't, `Block_release` falls back to `vm->compiler.builtin_free` (#458's
VM-only fallback), a synthesized Obj with no source token — the
function-prototype pass's `from_primary` filter always drops a tok-less Obj,
so the generated C called an undeclared `free()` (#990, fixed): whenever
`Block_release` is reachable, `serialize_block_preamble` now emits an
explicit `extern void free(void *);`, always compatible with a real
`<stdlib.h>` declaration if both end up in the output. `struct __cccc_block`
itself (plus the `Block_copy`/`Block_release` support above) is also emitted
for a TU that uses a block *type* but declares no block *literal* at all —
e.g. a function that only takes a block parameter — rather than only when a
literal is present (#990/#993). A block literal serialized at file scope
is a hard error — its descriptor is a local, and there is no enclosing frame
to hold one — though this is unreachable in practice today, since the VM's
own global-initializer constant-expression check already rejects a block
literal there before serialization is ever reached. A capture whose type is
itself a struct/union/enum declared inside a function (rather than at file
scope) is promoted ("hoisted") to file scope ahead of the env struct that
needs it, renaming its tag only on a collision with an unrelated file-scope
name of the same spelling (#989); a *tagless* local aggregate capture gets a
synthesized `__cccc_local_anon_N` tag rather than being inlined separately
at every use site. Two different functions each independently declaring an
identical `struct P` are treated as one type by this promotion (structural
equality, not declaration identity) — harmless, since the layout is
identical either way, but it means hoisting one's tag makes the other
resolve to the same file-scope name too. A *by-value* capture of a
*header-declared* type (e.g. `struct tm`) previously had the mirror-image
problem — the env struct was emitted ahead of the `#include` replay that
would complete the type — fixed (#993) by running the block preamble after
both mechanisms that can bring such a type into scope: the `#include` replay
itself, and (for a type reached only through a cccc-only-routed include
whose own `#include` is deliberately not re-emitted, #896) the file-scope
type-definition pass. In the `-c=generated` emit-events path specifically, a
captured `#include` is replayed interleaved with generated functions
(pinned there by #953) and still follows the block preamble, so a
header-type capture in *generated* code can still precede its `#include`;
this residual case is tracked as a follow-up ticket. Emitting `^{ }`
verbatim behind a `-fblocks` opt-in, for callers who want clang dialect
fidelity instead of this lowering, is tracked as a separate follow-up
ticket too.

A by-value capture of an aggregate (`struct`/`union`/array) larger than one
machine word, a wide `_BitInt`, or a `_Decimal128` used to be silently
truncated to its first 8 bytes by the VM's own block-literal codegen (#994):
the descriptor was a flat one-8-byte-slot-per-capture array, and every
capture copied through exactly one 8-byte load+store regardless of its real
size — a wide `_BitInt`/`_Decimal` capture (address-based storage) fared
worse still, storing a pointer *into the enclosing frame* rather than a
snapshotted value, dangling the instant that frame exited. This was VM-only
— the native serializer already sized each env struct field from the
capture's real type and copied it with plain struct assignment (valid C for
every aggregate kind except a bare array, which needs `__builtin_memcpy`
instead — also fixed by #994). Fixed by sizing each descriptor slot from the
capture's own type (8 bytes for a scalar, a `__block` heap-box pointer, or a
`TY_VLA`'s placeholder pointer; `align_to(size, 8)` otherwise) instead of a
flat one-word-per-capture array, and copying a wide slot via the VM's
existing `MCPY` opcode instead of a truncating 8-byte load+store. A second,
independent gap surfaced by the same fix: a struct/union/vector/wide-
`_BitInt`/`_Decimal` *parameter* is itself passed by pointer (its own frame
slot holds a pointer to the value, not the value's bytes — the same ABI
fact `gen_addr`'s plain variable-read path already accounts for), so
capturing such a parameter needed one extra pointer dereference before the
capture-copy loop's source address was usable.

A `NodeKind`/`TypeKind` with no serializer case above is a hard compile error
(#963c), not a divergence: `serialize_expr`/`serialize_type`'s `default:` arms
used to emit a `/* unsupported expr kind N */`/`/* unknown type */` comment and
keep going, which in statement position was a syntactically valid null
statement — the construct silently vanished from the native binary while the
VM still ran it. Every kind that reaches the serializer today has an explicit
case (this document's four divergences above, plus the intentional-drop and
hard-error constructs elsewhere in this section); anything that doesn't is
rejected with a diagnostic naming the kind, so a future gap fails at
implementation time instead of silently miscompiling a program that runs
correctly in the VM.

A multi-file build's serialized output no longer duplicates or misspells a
few constructs a real host compiler would reject (#999). A `static`
function *defined* in a plain `#include`d header and reached from more than
one translation unit used to be re-emitted once per TU — internal-linkage
functions are deliberately left uncanonicalized across TUs by
`cc_link_progs` (#957), so each TU's own Obj carried a full copy of the
body — producing a "redefinition" error from the host compiler the moment
more than one input file shared such a header. It is now skipped entirely
(both its forward declaration and its definition) whenever the header's own
`#include` is already being replayed into the output, the same
`from_primary`/`path_is_captured` reasoning the rest of this section
already documents for a type. A scalar (non-struct/union/enum) typedef,
e.g. `typedef unsigned long DyValue;`, previously always spelled as its
canonical underlying type in a function's parameter/return position,
dropping the typedef from the output entirely — cosmetic on a platform
where the typedef and its canonical spelling denote the same real type, but
a hard "conflicting types" error where they don't (`uint64_t` is `unsigned
long long` on LP64 Darwin, not `unsigned long`, so re-declaring a
`uint64_t`-typed parameter as `unsigned long` collides with that function's
real prototype in a header the output also includes). The typedef's own
spelling is now preserved by matching the parameter/return `Type`'s
identity (walking the `copy_type()` chain a typedef declaration and each of
its uses shares) against the recorded typedef table; struct/union/enum
typedefs are untouched, since they already had a working, independent
structural-matching alias mechanism that predates this fix. A `static
const` global initializer taking the address of a function declared (but
not yet defined) later in the same file — a vtable of function pointers to
file-static functions, `static const VT k = { .open = later_fn };` — used
to reach the output with nothing declaring `later_fn` yet, since the
global-definitions pass ran ahead of the function-prototype pass; a
targeted pass now forward-declares exactly the functions a global's
initializer relocations reference, without hoisting every prototype
unconditionally (which would reopen #953's struct-tag-scope hazard). That
targeted pass had its own gap, found and fixed under #1151: it forward-declared
*every* such function unconditionally, including one whose declaration is
already header-supplied (e.g. a vtable naming a real libc function directly,
`static FfiOps ops = {strlen, strcmp};`) — emitting a second prototype
spelled in CCCC's own bundled-header types, which collides with the real
one the replayed `#include <string.h>` already brought into scope
("conflicting types for 'strlen'"). Fixed by giving the pass the identical
header-supplied suppression the ordinary function-prototype pass already
had (factored into one shared predicate), deliberately without adopting
that other pass's `is_implicit`/macro-generated skip arms — those would
reintroduce the "nothing in scope" bug #999 fixed in the first place.
Separately, a translation unit holding only typedefs/prototypes and no
definitions is not a compile failure — `parse()`'s own contract returns
`NULL` for that case, previously treated by `main.c`'s per-TU loop as
unconditionally fatal (with the process exit code left at 0 regardless).
Two more gaps found alongside #999 and left for separate follow-up tickets
are now fixed too. First (#1002): `function_is_header_supplied()`'s
`from_primary` check — "was this static function's declaring file the
*primary* input file" — meant a static function with a body, defined in any
*non-first* command-line input file, was misidentified as header-supplied
and silently dropped from `-m`/`-c=native` output. Fixed by
`file_is_command_line_input()`, matching against every command-line input
path, not just the first. Once fixed, the ticket's own scenario becomes
observable: `cc_link_progs` deliberately leaves `static` (internal-linkage)
Objs uncanonicalized across TUs (#957), so two different `.c` inputs each
defining `static int helper(void)` with no shared header contribute two
same-named Objs to the merged output. Fixed by `rename_colliding_static_names()`,
run once after `rename_anon_globals()`: every same-named `static` Obj but
the first, among Objs declared in more than one distinct file, is renamed
in place. A name with no collision is left exactly as written.

Second (#1003): a plain `#include` of a header whose CCCC copy is the only
implementation likely to exist on a typical host at all (`stdbit.h`,
`stdckdint.h`, `threads.h`, `uchar.h`, `Availability.h`, `decimal_math.h`)
used to be replayed verbatim like any other standard header — correct for a
header the host is expected to have, but an unresolvable `file not found`
for one of these, even though CCCC itself compiled the program fine.
`is_cccc_supplied_only_header()` marks such a header cccc-only the moment
the `PP_INCLUDE` handler resolves it (on both the on-disk and
embedded-`src/std.c`-table branches), reusing the #896/#999 cccc-only
machinery wholesale: the `#include` is suppressed and the header's own
content is re-derived into the output instead. `decimal_math.h` is the one
exception — its `static inline` wrappers bottom out in `extern
__cccc_dec_*` symbols that exist only inside the VM's FFI runtime, so
re-deriving would only trade one unresolvable reference for another; it is
a hard compile error instead, matching the existing `_Decimal`
serialization refusal.

Preprocessor macro definitions and `#pragma once`/include-guard state are
now independent per translation unit (#1001), the way a standards-conforming
multi-file build requires — previously they were shared across every input
file one `cccc` invocation compiles together: a `#define` in one `.c` file
was visible in another with no `#include` at all, and a header's include
guard, once tripped by the first TU to include it, silently emptied that
same `#include` for every later TU. This was the root cause blocking a full
end-to-end reproduction of #999's own dandy (`~takeiteasy/dandy`) repro,
since dandy's shared header carries a guard. Fixed by
`cc_reset_preprocessor_state_for_next_tu()` (`main.c`'s per-TU preprocess
loop, between files, never before the first): the macro table is restored
to a fresh copy of the `-D`/`-U` baseline captured right after CLI
processing, and `pragma_once`/`include_guards`/`included_headers`/
`guard_macros` are all cleared. A second, load-bearing bug had to be fixed
in the same change: `parse()` entered a file scope per translation unit
with no matching `leave_scope()` at all, so every TU's declarations stayed
reachable from every later TU's own parse — silently masking the guard leak
above (a guard-emptied `#include` produces nothing new, but the earlier
TU's declarations were still visible through the leaked scope chain).
`cc_leave_top_file_scope()`, called from `main.c`'s parse loop between
files, closes this — except after the *last* file, since a comptime macro
function's body can reference a runtime symbol via the `$identifier`
reflect operator, and macro compilation runs after every real TU has
already been parsed, relying on the last TU's file scope still being live.

One consequence worth calling out: a header that *defines* (not just
declares) a non-`extern` global and is `#include`d, unguarded per TU, from
more than one translation unit now produces a "redefinition" error —
correct, matching what a real linker does with the equivalent two-`.o`
build (verified against a real host `cc`/`ld`), but a change from the
previous (masked-by-the-scope-leak) behavior for anyone who had, likely
unknowingly, relied on it.

Two translation units that each independently *complete* a same-named but
differently-shaped struct/union/enum tag — the opaque-handle idiom used
per-backend, where a shared header only forward-declares the tag and each
`.c` privately supplies its own `struct Foo { ... };` — no longer collide in
`-c=native`/`-m` output (#1014). `same_type_or_origin()` already treated the
two completions as different types (tag matches, member-wise comparison
fails), so they were never wrongly deduplicated, but nothing renamed them
apart either, and both reached the output under the identical plain tag
name, producing a host "redefinition" error. Every colliding group but one
is now renamed to `<name>__cccc_dup<N>` (sharing #1002's suffix and
counter). At most one group can keep the plain spelling: the output still
replays the shared header's own `#include`, which binds its uses of the tag
*textually*, so whichever group is "header-exposed" always wins regardless
of which `.c` is listed first on the command line — renaming that group
instead would turn a working replayed prototype into a "conflicting types"
error. If a replayed header genuinely exposes entities of *both* shapes —
which cannot happen from the opaque-handle idiom itself, only from a
header that inconsistently declares more than one signature over the same
tag — the collision is unrepresentable in flat C by any renaming; the pass
still renames deterministically (first-created wins) rather than leaving
the ambiguity unresolved, and the host compiler reports whatever residual
conflict remains. One related gap is known and not fixed by this change:
`same_type_or_origin()`'s member-wise comparison does not consider bit-field
width, so `struct S { int x : 1; };` and `struct S { int x; };` are treated
as the same shape and left uncollided (pre-existing, unrelated to this fix).

Renaming a colliding `enum` tag does not by itself rename its
*enumerators* — two enums sharing both a tag and an enumerator name (or, more
generally, any two distinct enums sharing an enumerator name, tags colliding
or not — a tagless `typedef enum { ... } T;` included) previously still
collided on the enumerator even after the tag itself was renamed apart. Fixed
separately (#1015): every enumerator name shared by two or more distinct enum
groups is renamed the same way, to `<name>__cccc_dup<N>`, following the
identical header-exposed/keeper rules described above so the two passes never
disagree about which group keeps the plain spelling. The same unrepresentable
case applies here too (more than one genuinely header-exposed group sharing
an enumerator name) — renamed deterministically, host compiler reports what
remains.

An enumerator colliding with an ordinary file-scope identifier — `static int
AA;` in one file, `enum E { AA };` in another, and equally an extern global
or a function of the same name — was not caught by either rename pass above,
since neither ever looked at the other's namespace even though C has one
ordinary identifier namespace at file scope. Fixed (#1016) by widening
`rename_colliding_enum_constants()` to also build the set of every emitted
file-scope Obj name (including a bare prototype or `extern` declaration, not
only a definition — unlike `rename_colliding_static_names()`'s own scan,
which only cares about two *definitions* colliding) and treat a name in that
set as occupying the namespace unconditionally: the Obj is never renamed
(renaming an external-linkage Obj would change its emitted symbol and break
linking against anything outside these translation units; renaming a unique
`static` would widen #1002's "only rename dups" contract for a shape only
reachable from a TU that doesn't include the shared header), so every
colliding enum group's copy of the name is renamed instead — this composes
for free with an enumerator that also collides with another enumerator.
Tier 1 stays a hard rule even here: a header-exposed enum group's
enumerators are still never renamed. One case remains genuinely
unrepresentable and is not fixed: a header-exposed enum group's enumerator
colliding with a file-scope Obj in a `.c` that doesn't include that header —
neither side can be renamed without breaking something else. See below
(#1017) for how this residual is now at least diagnosed.

A header-exposed enum group's enumerator colliding with a file-scope Obj in a
`.c` that doesn't include that header (the residual #1016 left open, above)
remains genuinely unrepresentable in flat C by any renaming — the replayed
`#include` binds the enumerator's spelling textually inside the header
itself, and the Obj can't be renamed either (external linkage makes that
unsafe in general; a unique `static` renamed here would widen #1002's
"only rename dups" contract). The collision is still left in the generated
output for the host compiler to report, but it is no longer silent: `-m`/
`-c=native`/`-c=generated` now emit a `-Wnative-name-collision` warning
(part of `-Wall`, category `CCCC_WARN_NATIVE_NAME_COLLISION`) pointing at
the colliding declaration and naming both the enumerator and the header
that exposes it (#1017). This matters most under `-c=native`, where the
host compiler's own diagnostic otherwise names a temp file (deleted before
the invocation returns) with no indication which of the user's own source
files was responsible, or that a cccc renaming limitation — rather than an
ordinary naming mistake — is involved. Emitted (not a hard error) at the
exact point `rename_colliding_enum_constants()` (`src/serialize_program.c`)
already decides tier 1 forbids the rename, so it needed no new analysis;
kept as a warning rather than promoted to a compile error because the Obj
set that check consults has no `is_defining`/header-supplied filter, so an
Obj later excluded by `function_is_header_supplied()`'s narrower logic
could in principle produce a false positive — for a hard error that would
mean rejecting a program that currently compiles, which #1014 already
rejected as a design for this same class of residual. The warning covers
only this one case today; the #1014/#1015 tag-vs-tag and
enumerator-vs-enumerator unrepresentable cases described above do not yet
emit it, though the category is deliberately generic enough to extend to
them later.

The opposite direction — two *unrelated* typedefs whose underlying shapes
happen to be structurally identical, not two independently-completed copies
of one shared tag — used to collapse into a single printed type (#1091).
Found verifying #1090's `div`/`ldiv`/`lldiv` fix, in a native round-trip
using both `ldiv_t` and `lldiv_t` in one TU: on every 64-bit target this
project supports, `long` and `long long` share a representation, so CCCC's
own `typedef struct { long quot, rem; } ldiv_t;` and `typedef struct { long
long quot, rem; } lldiv_t;` are byte-identical. `find_typedef_name()`
(`src/serialize_type.c`) matched by `same_type_or_origin()`'s structural
(member-wise) fallback — deliberately load-bearing elsewhere, since it's
what dedupes two independently re-parsed copies of *the same* declaration
under comptime re-parse or #1001's per-TU preprocessor isolation (#1006,
#1046) — so only the second-collected of two structurally-identical
tagless typedefs was ever printed, and every use of the first (including a
function prototype's own return type) was spelled with the second's name; a
real host compiler then rejects the resulting type mismatch. A third,
narrower symptom of the same root cause: a tagless typedef structurally
identical to a separately-tagged struct got spelled with the tagged
struct's own tag (valid C, but the wrong type identity) via
`type_has_tag_for_owner()`'s own structural match.

Fixed without touching `same_type_or_origin()` itself (its structural
fallback stays exactly as load-bearing as before): `find_typedef_name()`
now tries pointer/origin identity first (`find_typedef_name_exact()`'s
existing `->origin`-chain walk, already used elsewhere for a non-aggregate
typedef, #999), falling back to the structural scan only when identity
finds nothing — this alone resolves an *ordinary* reference to a typedef
correctly, including one reached through a per-declarator `copy_type()`
copy. `find_tag_name()`/`type_has_tag_for_owner()` gained a
`tag_spelling_mismatch()` guard: a tagless type is never spelled with (or
considered to already have) a same-shaped tagged type's own tag — provably
safe, since two `Type` objects sharing a pointer anywhere on their
`->origin` chains necessarily agree on `struct_tag` already, so this can
only ever suppress a purely structural coincidence, never a genuine
identity match. Finally, the two struct/union/enum-definition dedup sites
(`ctx->defs`, populated by `collect_type()`'s post-order walk, and
`ctx->emitted_defs`, the typedef-dependency-chase skip-set) both gained a
nominal-aware variant (`type_vec_push_nominal()`/`nominally_distinct_
typedefs()`) that still dedupes two `Type` objects reachable from one
another via `->origin` (the same genuine-identity check as above) or
resolving to the *same* typedef name (#1006/#1046's re-parse case), but no
longer dedupes two structurally-equal objects that resolve to *different*
typedef names — each such nominally-distinct type now gets its own printed
definition. Verified via `tools/comptime_native_smoke.py` case 129 and
`tests/test_serialize_typedef_identity_1091.c` — a pre-fix build fails
outright under `-c=native` (a hard "redefinition" or "assigning to X from
incompatible type Y" from the host compiler), not merely wrong text, so
both are round-trip proofs (VM 42 → native 42), not just `-m` text
assertions.

Separately, `--checked-pointers` enforcement is VM-only — those modes warn and
drop it; see [SAFETY.md § Checked Pointers](SAFETY.md#checked-pointers).

The VM tolerates a handful of pre-C23 constructs as warnings that a strict
host C compiler rejects outright once the emitted C reaches it under
`-c=native`/`-m`/`-c=generated`: an old-style implicit-int `main()` (or a
non-standard first-parameter type CCCC only warns about via `-Wmain`), and a
non-`void` function falling off its end without a `return` (`-Wreturn-type`
leniency). This is not a serializer bug — the emitted C is a faithful
re-statement of the source, and the source itself is not standard-conforming
C; the VM is simply more permissive than the C standard for these specific
shapes, matching how a debug build of a real toolchain might warn instead of
erroring on legacy code. `tests/test_c4.c` (a self-hosting `c4`-in-`c`
compiler exercised as a compile-stress case, not CCCC's own C4 bytecode
format) hits the implicit-int case; several of `tools/tests.py --native`'s
own `CCCC_EXPECT_STDERR` warning tests (`test_main_bad_argc_error.c`,
`test_warning_main_bad_params.c`, `test_warning_declarations_default.c`,
`test_warning_return_type.c`) are *deliberately* written this way
specifically to exercise the warning, so they hit it too — see
man/TESTING.md's "Native round-trip mode" section for the full list.

**(#1144, closed)** Implicit function declaration used to be the same kind of
VM-permissive/host-strict leniency — CCCC accepted a call to a wholly
undeclared function silently at every `--std=`, including its own C23
default, because the VM resolves such a call purely at codegen against the
FFI registration table (`find_ffi_function`, `src/codegen_regalloc.c`), with
no declaration needed at all. `-c=native` never emitted a prototype for the
guessed implicit signature (`obj->is_implicit`, `src/serialize_program.c`) —
it could collide with the real one from a replayed header — so the emitted C
referenced an undeclared function and the host compiler rejected it, one
step removed from the actual cause. Unlike `-Wmain`/`-Wreturn-type` above,
this divergence is now closed rather than merely documented: implicit
function declaration is a hard parser error at `--std=c99`/`c11`/`c17`/`c23`
(and their `gnu*` variants) and always under `-c=native` regardless of
`--std=` — matching what every real host C compiler does at C99 and later.
It survives only at `--std=c89`/`gnu89` under the VM/`-m`/`-c=generated`
paths (never `-c=native`), where it remains the warning it always was — see
[TOOLING.md § Supported Warning Names](TOOLING.md#supported-warning-names).

`reallocarray()` was a platform gap, not a serializer bug: CCCC's VM
implements it directly (registered in the stdlib), so the VM run always
succeeds; the native link depends on the host libc actually shipping the
symbol, which glibc/BSD libc does and Apple's libc (as of macOS 15/Sequoia)
does not. `-c=native` on macOS therefore used to fail to *link* a program
that calls `reallocarray()`. #1028 originally decided against fixing this,
reasoning there was "nowhere to place an inline polyfill without adding
exactly the kind of runtime dependency `-c=native` exists to avoid" — that
reasoning assumed the only options were linking against a CCCC-owned runtime
or nothing. #1155 found a third option consistent with #1028's own
constraint: an inline, header-free, `static`-scoped function
(`__cccc_reallocarray`, `serialize_reallocarray_shim`,
`src/serialize_shims.c`) emitted directly into the generated C alongside
every other native-accessor shim (`stdout`/`errno`/`FLT_ROUNDS`/etc, same
file) — this is source text baked into the single translation unit the host
compiler already builds, not a separate link dependency, so it adds nothing
`-c=native` doesn't already emit for those other shims. The overflow check
mirrors the VM-side `cccc_reallocarray` polyfill (`src/stdlib/stdlib.c`) so
both backends agree on the `ENOMEM`/ptr-untouched contract.

The C23 `fmaximum`/`fminimum`/`fmaximum_num`/`fminimum_num`/`fmaximum_mag`/
`fminimum_mag`/`fmaximumf`/`totalorder`/`totalorderf`/`totalordermag`/
`canonicalize`/`getpayload`/`setpayload`/`setpayloadsig`/`llogb`/`llogbf`/
`fromfpx`/`ufromfp` family is the same class of platform gap as
`reallocarray()` above on macOS, not a serializer bug there: CCCC's VM
implements the whole family directly, and its own `math.h` declares them
(so the emitted C *compiles* cleanly, unlike an earlier `intmax_t`-
provenance symptom this ticket also carried — resolved as a side effect of
#1021's header-guard fix), but nothing in macOS's libm *provides* the
actual symbols, so the native *link* fails ("symbol(s) not found"). Decided
(#1037): same reasoning as #1028 — no CCCC-owned runtime ships alongside a
`-c=native` binary, so left as a documented platform gap on macOS.

This one didn't fully round-trip on Linux/glibc either, for a second,
independent reason (RESOLVED, #1051): `-c=native`'s native `cc` invocation
never passed `-lm` to the host linker at all. This stayed invisible until
now because glibc 2.34+ folded the *common* math functions (`sin`/`sqrt`/
etc., confirmed linking fine with no `-lm`) into `libc.so.6` directly, but
this newer C23 family is still libm-only there (confirmed: undefined
reference without `-lm`, links and runs correctly with it) — every other
native math test in the corpus happens to only use already-libc-merged
functions, so nothing else had hit this yet. Fixed by always appending
`-lm` to the native `cc` invocation (`src/main.c`) — harmless everywhere
else, since an unused `-lm` is simply dropped by the linker.

With that fixed, a third and unrelated blocker surfaced on Linux (RESOLVED,
#1052): `__cccc_issignaling_d`/`__cccc_iseqsig_d` (backing the
`issignaling()`/`iseqsig()` macros) are CCCC-internal names with no real
host libc/libm equivalent to link against at all — in VM mode they route
to software bit-pattern implementations (`src/stdlib/math.c`), but
`-c=native` only *declared* them (via the header replay) and never emitted
a *definition*, unlike `stdin`/`stdout`/`errno`/etc. Fixed by extending
`native_accessor_shims` (`src/serialize_shims.c`) with four more entries —
`issignaling_{f,d}` mirror the VM's own bit-pattern check inline (not by
calling the other's shim, since a program can use `iseqsig()` without ever
calling `issignaling()` directly, which would leave that separate shim's
own definition unemitted and an undefined reference behind); `iseqsig_{f,d}`
additionally `#include <fenv.h>` directly in the shim text before calling
`feraiseexcept(FE_INVALID)`, since — unlike the existing shim entries,
which only reference names their own triggering macro's already-replayed
header guarantees — a program using `iseqsig()` has no guarantee `<fenv.h>`
was ever included.

With all three resolved, `test_math_c23_ieee.c` now round-trips VM 42 →
native 42 on Linux; only macOS's own permanent libm gap (#1037) remains,
so it moved from the general `NATIVE_SKIP_TESTS` table to the
macOS-specific `NATIVE_SKIP_TESTS_MACOS` one.

The C23 `%b`/`%B` binary integer conversions are the same class of platform
gap on macOS (#1120): CCCC's VM formats printf/scanf through its own C23
formatter (`src/stdlib/format_printf.c`/`format_scanf.c`), so binary
conversions work everywhere in VM mode; `-c=native` output calls real host
libc, and macOS 15 libc implements neither conversion — `printf("%b", …)`
emits a literal `b` and `sscanf("101010", "%b", &a)` reports zero matches
(measured directly). glibc has carried printf `%b` since 2.35 and scanf
`%b` since 2.38, but glibc's scanf has no `%B` conversion specifier at all
(confirmed on Ubuntu 24.04/glibc 2.39: clang's own format checker flags
`sscanf(..., "%B", ...)` as "invalid conversion specifier 'B'") — printf's
`b`/`B` are interchangeable there since the case only selects the `0b`/`0B`
prefix spelling on output, a distinction that doesn't exist for scanf
input, so Linux fails only the one `%B`-via-scanf case (#1162) rather than
the whole file the way macOS does. All three format-specifier tests live in
`tests/suites/test_suite_printf_c23.c`, skipped by the native corpus on
every platform via the general `NATIVE_SKIP_TESTS` table (moved there from
the macOS-specific `NATIVE_SKIP_TESTS_MACOS` one once the Linux-side gap
was confirmed, #1162). Decided: same reasoning as #1028/#1037 — no
CCCC-owned runtime ships alongside a `-c=native` binary, so libc lag is not
CCCC's to polyfill.

A sibling finding from the same sweep (#1120) needed no skip at all:
`realloc(p, 0)`'s contract is implementation-defined when no new memory is
allocated (C17/C23 7.22.3.5) — the VM and glibc free and return `NULL`,
macOS libc returns a valid zero-size block. `test_realloc_calloc`
over-specified the glibc answer; relaxed to assert only what both conforming
behaviours share (never the old pointer itself), matching #1066's
over-specification precedent.

#1052's four new `native_accessor_shims` entries above missed a step
`isnan`/`isinf`/`signbit`/`fpclassify`'s own entries had already needed
(#1021): `include/math.h`'s `__cccc_issignaling_f`/`_d` and
`__cccc_iseqsig_f`/`_d` **declarations** stayed unconditional `extern`s,
never guarded on `__CCCC__` like the block right above them. Once a
program calling `issignaling()`/`iseqsig()` reached `-c=native` with
CCCC's own `include/` on the search path (the test harness's own
`-I./include`, same shadowing hazard #1054 documented for `setjmp.h`), the
host compiler saw the unconditional `extern` declaration first and then
`native_accessor_shims`'s `static` definition — "static declaration
follows non-static declaration". Only surfaced on a real Linux run
(#1063); #1052's own verification never went through the real harness's
`-I` shape. Fixed by wrapping the four declarations in `#ifdef __CCCC__`,
matching the `isnan`/`isinf`/`signbit`/`fpclassify` block. **Invariant for
any future `native_accessor_shims` entry:** if CCCC's own header also
declares the name (rather than only defining the macro that calls it),
that declaration must be `#ifdef __CCCC__`-guarded — this is the third
time the same trap has been hit (#1021, #1023, #1052/#1063).

**Correction (#1066, closed — not a bug, the check was over-specified.)**
A first read of `-c=native`'s `fromfp(10000000000.0, FP_INT_TONEAREST, 8)`
(the width-8 overflow case) via the `cccc-linux-amd64` container looked
like a wrong result — the returned *value* differs from the VM's. Checked
against real glibc directly before filing a fix (this batch's own
recurring lesson): C23 7.12.9.6 only guarantees `FE_INVALID` is raised
when the correctly-rounded value doesn't fit in `width` bits — the
returned value itself is implementation-defined. CCCC's VM returns 0;
real glibc instead saturates to the widest representable magnitude (127
here). Both are conforming C23 implementations disagreeing on an
unspecified value, not a native-vs-VM divergence in anything CCCC
controls. `test_math_c23_ieee.c`'s two value-checking assertions were
over-specified against this implementation detail — relaxed to check only
`FE_INVALID`, matching what the file already did two lines below for the
identical case. `include/math.h`'s own `fromfp`/`ufromfp` block comment
corrected to state this explicitly rather than "else 0". No `-c=native`
serializer or VM change; `native_accessor_shims` calls the real host
`fromfp` directly, as intended by this header's polyfill scope.

Every `-c=native` compile used to only forward `-std=` to the host `cc`
when the user passed `--std=` explicitly on the CCCC command line — a
plain `cccc foo.c -c=native` relied entirely on the host `cc`'s own
default standard, which can be older than CCCC's own resolved default
(`gnu23`) and silently reject a legitimately-emitted C23 construct
(`true`/`false` keywords, `enum : T`, …) even though the VM run
succeeded (#1053). Forwarding CCCC's resolved default unconditionally
isn't safe either: a host `cc` that rejects `-std=gnu23` outright (e.g.
Ubuntu's plain `cc` → gcc 13, see man/TESTING.md's CI notes) would then
fail *every* `-c=native` compile, not just ones using C23 syntax. Fixed
by probing the host `cc` quietly (`run_argv_quiet()`, `src/exec.c` —
redirects the child's stdout/stderr to `/dev/null` so a rejected rung's
diagnostic isn't the user's business) down a ladder from CCCC's resolved
default toward older standards (`gnu23` → `gnu2x` → `gnu17` → `gnu11`,
or the `c*` spellings under `--std=c…`), forwarding the newest rung
actually accepted (`native_resolve_std_ladder()`, `src/main.c`). `gnu2x`
is its own separate rung, not an alias tried together with `gnu23`: gcc
13 accepts `-std=gnu2x` but rejects `-std=gnu23` outright (confirmed in
the `cccc-linux-amd64` container). If nothing in the ladder is accepted,
nothing is forwarded — identical to the pre-#1053 behaviour, so this
can never turn a native compile that used to succeed into a failure.

An explicit `--std=` on the CCCC command line is probed too (#1073), but
through a narrower ladder than the implicit default: only spellings of
the *same* standard the user named (`c23` → `c2x`, `c17` → `c18`, `c11` →
`c1x`, `c99` → `c9x`, `c89` → `c90`), never descending to an older one — a
user who wrote `--std=c23` must never silently get C17 semantics on the
native half while the VM half stayed C23. If no spelling of the requested
standard is accepted, the user's literal spelling is forwarded unprobed,
letting the host emit its own diagnostic — the same "host rejection is
the user's own stated intent" behaviour the pre-#1073 code always had.
Both ladders' spellings are confirmed against real Apple clang 17 and
Ubuntu GCC 13.3.0, not assumed to transfer from one compiler to the
other.

Every rung in both ladders is itself tried as `gnu<NN>` before `c<NN>`,
regardless of which prefix the user actually typed on the CCCC command
line — `--std=c89` probes `-std=gnu89` before falling back to `-std=c89`
(#1187). CCCC's own frontend treats the two prefixes identically
(`Compiler.c_std_gnu` has no reader outside `native_resolve_std_ladder()`
itself) and its pedantic-warning-not-error C89 dialect is permissive like
`gnu89`, not strict ISO C90, even when the user spells the non-GNU `c89`
form — so forwarding a literal `-std=c89` to a real host GCC (which *is*
strict ISO C90 under that spelling) rejected constructs CCCC's own
`--std=c89 -Wpedantic` only warns on (`//` comments, mixed declarations,
VLAs, compound literals, designated initializers), turning a native
compile that succeeded on clang and under the VM into a GCC-only failure.
A user who explicitly wrote a `gnu<NN>` spelling gets no `c<NN>` fallback
rung — they already asked for the GNU dialect, there is nothing narrower
to fall back to.

While implementing #1053, a second, unrelated bug (#1065) was found and
fixed in the same pass: `run_native_backend()` built each `-std`/`-D`/
`-U`/`-l` flag into a `char [256]` stack buffer and pushed the buffer's
address straight into the native `cc`'s argv — `argv_push()`
(`src/exec.c`) stores the pointer it's given, it does not copy. The
`-std` buffer was out of scope by `exec()` time, and each `-D`/`-U`/`-l`
loop's buffer was a fresh per-iteration local that (in practice) reused
one stack address across iterations, so e.g. `-DA=1 -DB=2` reached the
host `cc` as two copies of whichever define was assembled last. Fixed by
giving `run_native_backend()` its own heap-backed `StringArray`, freed
after the spawn — the same pattern `src/build.c`'s
`push_compile_flags()` already used correctly for the identical shape. A
second, independent bug in the same area was found while writing a test
for this: `parse_define()` (`src/main.c`) split each `-D` argument *in
place* at its `=` the first time it ran (to register the macro with the
VM's own preprocessor) — permanently truncating the very same string
`run_native_backend()` later reuses to build the native `cc`'s `-D`
flags, well before the pointer-aliasing bug above ever got a chance to
duplicate it. Fixed by having `parse_define()` split via a bounded copy
instead.

`aligned_alloc()` with a `size` that is not an integral multiple of
`alignment` is the same class of platform gap as `reallocarray()` and the
C23 libm family above, on macOS only (#1061). C11 originally required
`size` to be a multiple of `alignment` ("otherwise, the behavior is
undefined"); DR 460/N2072 removed that constraint in C17, so a call like
`aligned_alloc(256, 128)` is fully defined by C17/C23 and simply returns a
256-aligned 128-byte block. glibc 2.38+ implements the C17 wording and
accepts it; CCCC's own VM-heap `aligned_alloc` (`MALCA` /
`cccc_vm_heap_malloc_aligned`, `src/ops.c`) always has too, matching C23
(CCCC's default standard). macOS libc still enforces the pre-DR-460 C11
rule and returns `NULL` (`errno == EINVAL`) for the same call — confirmed
directly (both x86_64 and aarch64 glibc containers accept it, macOS
rejects it). A program relying on the now-conforming form gives the right
answer on the VM and under `-c=native` on Linux, and `NULL` under
`-c=native` on macOS. This applies to `--no-vm-heap` too:
`cccc_ffi_aligned_alloc` (`src/stdlib/stdlib.c`) falls through to the real
host `aligned_alloc` there, so the same macOS/glibc split shows up even
without the VM heap. Decided (#1061): documented platform gap, no VM
change — tightening the VM to match macOS would make CCCC's own reference
semantics non-conforming to the C23 standard it defaults to, just to match
one host libc that hasn't caught up to a six-year-old defect report; the
divergence belongs to macOS libc, not to CCCC.

**Plain `char` signedness (#1064, RESOLVED).** CCCC's own `ty_char`
(`src/type.c`) is signed on every platform, but a real host's plain `char`
isn't universally so — measured directly: `glibc`/aarch64 defines
`__CHAR_UNSIGNED__` (confirmed in the `cccc-linux-arm64` container),
`glibc`/x86_64 and every supported macOS arch do not. A GNU
`vector_size` lane read back as `*((char *)&v + i)` and compared against a
signed constant (`-1`, `-11`, …) — the pattern `-c=native` emits for a
`v16qi`/similar narrow-lane vector, e.g. `test_attr_vector_size_compare.c`/
`test_attr_vector_size_intdiv.c` — silently produced the wrong answer on
Linux/aarch64, with no compile error, since the divergence is purely in how
the two sides interpret the same bit pattern. Reproduced by hand-compiling
`-m` output with `-funsigned-char` on macOS and matching the exact reported
exit codes. Fixed by unconditionally forwarding `-fsigned-char` to the host
`cc` in `run_native_backend()` (`src/main.c`) — unprobed, unlike #1053's
`-std` ladder, since the flag has existed in both gcc and clang for decades
on every target. **Scope**: this only covers the compile `-c=native` drives
itself — `-m`/`-c=generated` output compiled by hand still needs
`-fsigned-char` passed explicitly to match CCCC's own signed-`char`
semantics on a host where plain `char` is unsigned.

**Captured conditional-group directives, not replayed (#1064, RESOLVED).**
`cc_serialize_program()`'s `emit_directives` replay loop (`src/serialize_program.c`)
used to replay a captured `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`
directive line verbatim into `-m`/`-c=native`/`-c=generated` output — always
an *empty shell* by the time it reaches that loop, since CCCC's own
preprocessor has already resolved which branch was taken and captured only
that branch's own content as its own separate lines/directives. Replaying
the shell anyway handed the *evaluation* to the host compiler a second time,
with two real hazards: (1) a host lacking a feature-test macro CCCC's own
preprocessor already resolved — clang 18 rejected a captured
`#if __has_embed(...)` shell outright (`test_has_embed.c`,
"function-like macro '__has_embed' is not defined"), even though CCCC
evaluated it fine and the shell carried no content; (2) a captured
`#ifdef __CCCC__` shell being silently false at the host (which never
defines that macro), which would drop whatever a taken branch inside it
captured. Fixed by dropping conditional-group directive lines from the
replay loop, gated off (i.e. still replayed) under `--emit-cccc`, matching
the loop's two existing per-line filters (cccc-only headers, `setjmp.h`) —
dialect-fidelity output expects a cccc-aware reader that understands the
routing syntax anyway.

**Captured non-ASCII-macro-name `#define`/`#undef` lines, not replayed
(#1118, RESOLVED).** The same replay loop used to hand every auto-captured
directive line from command-line inputs to the host compiler verbatim —
including lines whose macro *name* is non-ASCII (emoji identifiers, an
accepted CCCC extension; `tests/suites/test_suite_misc.c`'s worm/snake
operator macros are the live example). Every in-AST use of such a macro is
already expanded at parse time, but the host preprocessor rejects a
non-ASCII macro name outright ("macro name must be an identifier", once per
`#define` plus its matching `#undef`), so replaying those lines failed an
otherwise-clean native compile — and no other replayed directive text can
legally reference such a name either, since the host applies the same
rejection there. Fixed by dropping `#define`/`#undef` lines whose name starts
with a non-ASCII byte from ordinary replay (`line_macro_name_is_non_ascii`,
`src/serialize_program.c`), beside the loop's existing per-line filters and exempted
under `--emit-cccc` like all of them. ASCII-named defines still replay
unchanged; a demand-driven replay (emit a captured define only when some
other replayed line references its name) was considered and rejected as
over-engineering — no consumer of define replay is known to remain post-#1114
(the LIMIT_EXPR-inside-#embed-limit case that motivated define replay is
gone).

**C11 `<threads.h>` lowering (#1088, RESOLVED).** `thrd_create`/`mtx_lock`/
`cnd_wait`/`tss_create`/`call_once`/etc. (`include/threads.h`) are VM cfuncs
(`src/stdlib/pthread.c`) with no host libc symbol behind them, so a
`-c=native` binary calling one used to fail at the host linker with an
undefined symbol and no CCCC-side diagnostic. `<threads.h>` was already on
`is_cccc_supplied_only_header()` (`src/preprocess.c`), so its `#include` was
already suppressed and its types (`mtx_t`/`cnd_t`/`thrd_t`/`tss_t`) already
re-derived correctly — only the function *definitions* were missing.

Fixed with a self-contained shim (`serialize_threads_shims`, `src/serialize_shims.c`),
each function a near-verbatim port of its VM cfunc counterpart minus the GIL
save/release dance and the `--thread-safety` lock-order bookkeeping, written
over the real host `<pthread.h>` already replayed by the #1022 hand-off —
**not** a `#include_next` hand-off onto a real host `<threads.h>` the way
`include/pthread.h` itself hands off. Two reasons: CCCC's own
`thrd_error`/`thrd_timedout`/`thrd_busy`/`thrd_nomem` encoding doesn't match
glibc's (folded to bare integer literals at guest compile time, so any
comparison other than `!= thrd_success` would silently change meaning had
glibc's own enum reached the output — the same `FP_*`/`isnan` asymmetry
`native_accessor_shims` documents), and Darwin has no `<threads.h>` at all,
so a hand-off would leave macOS permanently unsupported. A self-contained
shim consults the host's own `<threads.h>` on neither platform, closing both
in one change — the only residual is `thrd_t`/`mtx_t`/`cnd_t` staying
CCCC's own opaque-handle projections rather than the host's real types
(same shape as `<pthread.h>`'s own `pthread_mutex_t`/`pthread_cond_t`), and
`--thread-safety` lock-order checking remaining VM-only.

The VM's own lazy mtx_t/cnd_t handle allocation (`ensure_mtx`/`ensure_cond`,
`src/stdlib/pthread.c`) is check-then-malloc-then-store, safe only because
the GIL serializes every cfunc call — the native shim's own
`__cccc_ensure_mtx`/`__cccc_ensure_cnd` instead use a real atomic
compare-exchange on the `->__handle` field, since two threads racing that
check under `-c=native`'s genuine parallelism could otherwise each allocate
a host mutex and silently lock two different ones. `call_once` stopped
being a guest-side macro (a plain, non-atomic flag check safe only under the
VM's own GIL) and became a real function on both back ends, backed by an
atomic CAS; the native shim specifically needs a three-state protocol (not
started / in progress / done), not a plain two-state CAS, so a losing
thread spin-waits until the winner's initializer has actually completed —
matching real `pthread_once`/glibc's own blocking behaviour, which is what
C11 programs rely on in practice even though 7.26.6.2p2's literal text only
promises a happens-before ordering. `mtx_timedlock` mirrors the VM's own
`#if __linux__` / trylock-poll split for macOS (which lacks
`pthread_mutex_timedlock`) byte-for-byte, so both back ends agree by
construction.

**C11/C23 `<uchar.h>` conversions on a host lacking the real symbols
(#1141, RESOLVED).** `mbrtoc16`/`c16rtomb`/`mbrtoc32`/`c32rtomb`/`mbrtoc8`/
`c8rtomb` are VM cfuncs (`src/stdlib/wide.c`); `<uchar.h>` is on
`is_cccc_supplied_only_header()` like `<threads.h>` above, so its
declarations are re-derived but nothing defined the functions themselves
for a native binary to link against. glibc has shipped the c16/c32 pair
since 2.16 and the c8 pair since 2.36 — a host that new links against the
real symbol using the re-derived `extern` alone — but Darwin has never
shipped any of the six ("Undefined symbols ... _c16rtomb"). Fixed with
`serialize_uchar_shims` (`src/serialize_shims.c`), the same self-contained-shim
shape as `<threads.h>`'s own fix above: each fallback is a near-verbatim
port of its VM cfunc counterpart in `src/stdlib/wide.c`, emitted only for
the functions actually used and only under the identical `__GLIBC_PREREQ`
feature test `wide.c` itself already gates its own choice on, so a host
with the real symbols never sees a second, competing definition. The two
copies have no shared source and are kept in sync by hand; a follow-up
tracks folding them into one generated source shared by both build paths.

**`<dlfcn.h>` policy parity (#1105, RESOLVED).** `dlopen`/`dlsym`/`dlclose`/
`dlerror` are real host libc symbols (unlike `<threads.h>`/`<uchar.h>`
above), so `-c=native` used to forward guest calls straight to the host's
libdl — correct for `dlopen`/`dlsym`, but not for `dlclose`: the VM's own
registry (`cccc_rt_dlclose`, `src/vm.c`) refuses to close a handle with any
still-"live" `dlsym`'d symbol (`live_symbol_count`, incremented on every
`dlsym`, never decremented — once any symbol has been resolved through a
handle, that handle can never be closed), while a bare host `dlclose()`
enforces no such thing. `tests/suites/test_suite_ffi.c`'s `test_dlfcn`
asserted the VM's refusal and got the host's success instead (3, not 42).

Fixed with a registry shim (`serialize_dlfcn_shims`, `src/serialize_shims.c`),
the same shape as `<threads.h>`'s/`<uchar.h>`'s own self-contained shims
above but layered *over* the replayed real `#include <dlfcn.h>` rather than
replacing it (`dlopen`/`dlsym`/`dlclose`/`dlerror` are renamed to
`__cccc_native_dl*` — same `rename_bundled_extern_for_native_shim`
mechanism `<poll.h>`'s shim uses below — so the shim body is free to call
the real host functions under their original names). The registry cannot be
keyed by the *host* handle: `dlopen(NULL)` returns the same pointer on
every call, while the VM mints a fresh token (with its own live-symbol
count starting at 0) per `dlopen` — keying on the host handle would let one
subtest's `dlsym` poison a later, unrelated subtest's `dlclose` in the same
process (exactly what `--testing=native`'s single generated harness process
does, running `test_dlfcn`/`test_dlfcn_close_no_symbols`/`test_dlfcn_missing`
back to back). So the guest's `void *handle` is an **opaque per-open
token** (a registry node's own address), exactly like the VM's own token —
it must never be passed to anything but these four functions. Nodes are
never freed or reused, matching the VM's own registry, which lives until VM
teardown; the table is unbounded, not capacity-limited. Every mutation goes
through `__atomic_*` builtins (never `<stdatomic.h>`, for the same reason
`<threads.h>`'s shim avoids it) since, unlike the VM's GIL-protected
registry, this table has no serialization of its own. The error slot is
`_Thread_local` — a deliberate *improvement* over the VM, whose
`vm->dyn_error` is one field shared by every guest thread under the GIL —
and, matching `cccc_rt_dlerror` exactly, is **not** cleared on read; each of
`dlopen`/`dlsym`/`dlclose` clears it on entry instead.

`include/dlfcn.h`'s `RTLD_*` macros are derived from the real host
`<dlfcn.h>` this `cccc` binary was built against, not hand-transcribed, so
`mode` being forwarded to the host `dlopen()` unchanged is correct on
every platform — a guest asking for `RTLD_GLOBAL` on macOS gets macOS's
own value (`0x8`), not glibc's (`0x100`, which collides with macOS's own
`RTLD_FIRST`). Deliberately not provided: the `dlsym` pseudo-handles
(`RTLD_NEXT`/`RTLD_DEFAULT`/`RTLD_SELF`/`RTLD_MAIN_ONLY`) — `dlsym`'s
handle argument is resolved through the registry above, not a raw host
handle, so a pseudo-handle would behave differently between the VM and
`-c=native`.

