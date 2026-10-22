# Checked-pointer interfaces and casts

Contributor notes moved from the user guide `safety-checked-interfaces.md`: how call-argument checks, bounds casts and `dynamic_check` are implemented.

**Bounds-safe interfaces**. An annotated pointer parameter is itself
the bounds-safe interface Checked C calls an `itype` — no separate marker is
needed, because a checked attribute already imposes zero restriction on an
unchecked caller:

```c
void sink(int * [[cccc::array, cccc::count(n)]] p, int n);   // the interface
```

A prototype-only declaration's bounds token span is never resolved into a
real `Obj`-rooted expression (there is no body scope to resolve it into —
see the "Bounds declarations" paragraph above), but it is not dead: the
first time any call to this callee is compiled, `resolve_param_checked_
bounds()` (src/parse_core.c) resolves the same token span, separately, into
a reusable **template** — a synthetic scope of placeholder `Obj`s, one per
named parameter (`Obj.checked_self_param_idx`), pinned to **file scope**
specifically so a bound naming a global always resolves against the
declaration's own visible globals, never a call site's. Cached on the
callee's function type, so this runs once no matter how many calls follow.

At each call site, `rewrite_checked_call_args()` (src/parse_checked.c)
substitutes every placeholder in the template with a clone of that call's
own actual argument expression, and rewrites the qualifying argument from
`arg` into `(__ca = arg, __ca)` — `__ca` a fresh, ordinary declared-checked
local carrying the substituted template as its own bounds. No new opcode
and no new codegen: this is deliberately the same `(__cv = ..., __cv)` shape
`checked_cast_desugar()` uses for `[[cccc::assume]]`/`[[cccc::dynamic]]`
above, so the *existing* `verify_checked_assign_bounds()` pass discovers the
resulting `__ca = arg` assignment on its own ordinary post-parse walk and
emits the same `CHKAB` pair a hand-written assignment would get — the
call-argument case needed no dedicated enforcement mechanism of its own.

```c
void sink(int * [[cccc::array, cccc::count(n)]] p, int n);

int * [[cccc::array, cccc::count(2)]] small = ...;
sink(small, 8);   // traps: small's own bounds (2) don't imply the call's
                  // own claimed count(8)
```

A call gets no check at all unless **both** hold:

- The argument is itself a **directly declared-checked** source
  (`find_checked_base()` + `checked_base_is_declared()` — the "kind
  1", not a propagated local, which can hold the OPT sentinel range `CHKAB`
  has no sentinel-aware variant for). An ordinary unchecked `int *raw`
  argument gets no check — unchecked-to-unchecked interop is unaffected.
- Every OTHER parameter the bounds expression references (e.g. `n` in
  `count(n)`) is fed, **at this call**, by a side-effect-free argument —
  checked with `node_has_side_effects()`, the same predicate ordinary bounds
  resolution already uses for the identical "this is re-evaluated, so it
  must have no side effects" reason. `sink(small, 8)` (a literal) and
  `sink(small, len)` (a plain variable) both qualify; `sink(small, f())` and
  `sink(small, i++)` are silently declined — exactly the same trade-off
  `docs/safety.md`'s member-bounds section already makes for `f()->p[i]`: an
  extra evaluation of a side-effecting expression is not an acceptable
  price for a check.

`[[cccc::single]]`'s implicit `[p, p + sizeof(T))` needs no parameter
substitution at all and still gets a real caller-side check. A `_Checked[N]` array argument decays into an ordinary declared-checked
pointer before this rewrite ever runs, so it is covered with no special
casing. A prototype and a later definition declaring **different** bounds
for the same parameter is accepted silently in v1 — whichever declaration
was parsed first is the `Type` every call site's template resolves
against — a disagreement diagnostic is a filed follow-up.

Gated on `--checked-pointers` at parse time exactly like `verify_checked_
assign_bounds()` itself; with the flag off, `rewrite_checked_call_args()`
never runs, so `-m`/`-c=native`/`-c=generated` output is unaffected — no new
attribute, no new type spelling, nothing new for the serializer to strip.

An annotated **same-TU** function's own body stays `CHKR`-checked against
its own declared bounds, unchanged — this can newly trap
code that was never touched by an unchecked caller, since the callee is
violating its own declared contract regardless of who calls it. This is a
stated v1 decision, not gated off: the callee's checks use its *own*
declared bound, not the caller's real storage, so an unchecked caller
passing a genuinely too-short buffer still does not trap there — closing
that gap at the call boundary is the entire point of this feature.

Out of scope for v1, filed as follow-ups: return-position bounds
(`int * [[cccc::array, cccc::count(4)]] f(void);` parses today and is
entirely unconsumed); non-trivial-argument coverage via pre-call hoisting;
a warning when a check is silently declined; a prototype/definition
bounds-disagreement diagnostic; a propagated argument (excluded for
the same sentinel reason `verify_checked_assign_bounds()` excludes one);
struct-field bounds interfaces; and an annotated libc/POSIX bounds table as the adoption path that would make this
useful for `memcpy`/`fread`/etc. out of the box. No call-boundary ban exists, so an annotated prototype was already callable from a checked region; this feature only adds the enforcement.

**Bounds casts**. Every mechanism above assumes a pointer is *already*
declared checked — none of them explain how an unchecked pointer gets into
the checked world in the first place. `[[cccc::assume]]` and
`[[cccc::dynamic]]` are that entry point, the two Checked C bounds-cast
primitives, attached in a **cast's type-name** (not a declarator — a
compile error otherwise) alongside the checked kind and bounds form being
claimed:

```c
int *raw = ...;
int n = 5;

// assume: take the claim on trust, no check of it
int * [[cccc::array, cccc::count(n)]] p =
    (int * [[cccc::array, cccc::count(n), cccc::assume]])raw;

// dynamic: verify the claim against the SOURCE's own declared bounds
int * [[cccc::array, cccc::count(k)]] src = ...;
p = (int * [[cccc::array, cccc::count(n), cccc::dynamic]])src;
```

Both require a checked kind; with `array`/`ntarray` a concrete bounds form
is also required (`bounds(unknown)` is accepted for `assume` — there is
nothing to verify either way — and rejected for `dynamic`, since it too has
nothing to verify against). `assume` and `dynamic` are mutually exclusive.

**Desugaring.** The cast rewrites to `(__cv = (T [[...]])expr, __cv)`,
where `__cv` is a fresh, compiler-generated local declared with the cast's
own checked kind and bounds — an ordinary declared-checked variable from
every other pass's point of view. This is why `assume`/`dynamic` need no
new resolution machinery: `CHKR` emission, the propagation pass, and
`CHKAB` verification all already know how to handle a declared-checked
local, so making `__cv` a real one (rather than teaching every consumer of
`find_checked_base()` a new `ND_CAST`-rooted case) reuses the whole existing
pipeline. A direct dereference of the cast's own result —
`((T [[...]])q)[i]`, with no intervening assignment — still finds `__cv`'s
bounds, via a narrow unwrap used only by the dereference-bounds attachment
path (deliberately **not** exposed to the propagation/`CHKAB` passes: those
snapshot a source's bounds into a temp *before* the enclosing statement's
own store runs, an ordering `__cv`'s own store — itself a side effect of
evaluating the very expression being snapshotted — would violate). Gated on
`--checked-pointers` at parse time, same as the propagation and `CHKAB`
rewrites: with the flag off, the cast stays a plain cast and
`-c=native`/`-m`/`-c=generated` output is unaffected.

**`dynamic` reuses `CHKAB` verbatim** — the desugar's own `__cv = (T)expr`
is exactly the `q = E` shape `verify_checked_assign_bounds()` already
recognises (a declared-checked target, a declared-checked source), so no
new opcode was needed. The source must be a **directly declared-checked**
base with a resolvable bounds form — not a propagated pointer (would need a
sentinel-aware `CHKAB` variant, tracked as a follow-up) and not
`bounds(unknown)` — or the cast is a **compile error**, always on
regardless of `--checked-pointers`:

```
error: 'dynamic' cast source has no known bounds to verify against --
  use 'assume' to take responsibility for this conversion, or give the
  source a bounds declaration
```

This is deliberate, not an oversight: falling back to `--bounds-checks`'
`CHKB`/`CHKD` would silently do nothing for a stack or global source — see
"Why this exists" above. Assigning the cast's result into ANOTHER
declared-checked variable (`p = (T [[..., dynamic]])q;`) can trigger a
*second*, independent `CHKAB` pair from the ordinary assignment-scan pass
(comparing `__cv`'s bounds against `p`'s own declared bounds) — harmless, a
tighter restatement of the first, the same "one redundant check" shape the
propagation pass already documents for a checked RMW.

**`assume` never verifies its own claim.** The desugar's `__cv = (T)expr`
assignment is explicitly skipped by the assignment-scan pass (`Obj.
checked_cast_kind == CC_ASSUME` is checked before matching) — taking the
claimed bounds on trust, with zero runtime check, is the entire point.
Every access made *through* `__cv` afterward is an ordinary checked access
and is still `CHKR`-checked against the claim, same as any other checked
pointer — `assume` only skips checking the *conversion itself*, never
anything downstream of it.

**Inside a checked region** (`[[cccc::checked]]`/`#pragma cccc checked`,
below), these two attributes are exempt from the region's own cast ban —
they ARE the sanctioned way to change a pointer's checked kind there,
replacing the informal "just cast it" pattern (tracked separately as a
launder-cast hardening follow-up) with an explicit, auditable escape that
needs no `[[cccc::unchecked]] { ... }` wrapper.

v1 scope, all deliberate:

- A cast expression only — a checked-annotated function argument or return
  value is not covered (the same boundary `CHKAB` has).
- The cast must appear inside a function body; `vm->compiler.current_fn`
  must be non-`NULL` for `__cv` to have somewhere to live (a compile error
  at file scope, e.g. a global initializer).
- `dynamic`'s source must be directly declared-checked, not chained through
  `--checked-pointers`' own propagation.

**`dynamic_check`**. A programmer bounds assertion — `dynamic_check(cond)`,
spelled `__builtin_cccc_dynamic_check(cond)` or, for source compatibility,
`_Dynamic_check(cond)` (recognized positionally, an identifier not already
in scope immediately followed by `(`, so it never shadows a real function
of that name) — traps when `cond` is false, under `--checked-pointers`
(the VM's `CHKDC` opcode, or `__cccc_chkdc` in `-c=native`/`-m` output); a
no-op otherwise (parses to a plain `ND_NULL_EXPR`, the same convention
`__builtin_assume` uses). `cond` must be side-effect-free, same
rationale as a `count()`/`byte_count()`/`bounds()` expression: it is only
evaluated under the flag today, and a planned static-elision analysis is
expected to consume `dynamic_check` facts as proven truths without ever
evaluating them at all — a pure condition is required for that to be
sound.

**Runtime enforcement is opt-in, off by default**: `--checked-pointers` /
`#pragma cccc config(checked_pointers = true)`. Not part of any
`-0`/`-1`/`-2`/`-3` preset. The compile-time rules (attribute parsing, type
checking, the arithmetic-rejection diagnostic on `single`) are always on
regardless of this flag — only the `CHKR` runtime check itself is gated.

**Native and serialized output.** `-c=native` and `-m`/`--dump-expanded`
enforce `--checked-pointers` (or `config(checked_pointers)`) in the C they
emit: each check becomes a call to a small helper emitted at the top of the
output (`__cccc_chkr`, `__cccc_chknt`, `__cccc_chkab`, `__cccc_chkdc`). A
failed check prints the same banner as the VM, without the `PC:` line, and
exits with status 255:

```
$ ./cccc -c=native --checked-pointers -o oob oob.c && ./oob; echo $?

========== CHECKED BOUNDS VIOLATION ==========
Checked-pointer access out of declared bounds
Address:       0x16d63a454
Access size:   4 bytes
Declared bounds: [0x16d63a448, 0x16d63a454)
================================================
255
```

Without the flag the output carries no checks, helpers or temporaries.
Comptime code still runs in the VM, so under the flag its checked accesses
are enforced there too.

`-c=generated` emits only macro-generated code, so it has no user accesses
to check: it prints a `-c=generated ignores VM runtime safety/debug options`
warning and drops the flag. The `[[cccc::single/array/ntarray]]`/`count`/
`byte_count`/`bounds` attributes themselves are always stripped from
`-E`/`-m`/`-c=generated`/`-c=native` output (ABI-transparent, no change to
unchecked callers). The compile-time contract checks — `single`-pointer
arithmetic rejection, bounds side-effect rejection, a member bounds
expression naming an enclosing local or a bit-field sibling — are frontend
checks independent of `--checked-pointers` and apply in every mode.
