# Checked Pointer Bounds Propagation

How a pointer's bounds follow assignments.

**Bounds propagation across assignment**. `q = p + k;` on its own
still checks against `q`'s own declared bounds, not `p`'s — a plain
unchecked `q` has none, so by itself this would mean no check at all. But
when `q`'s *declaration* is one specific, statically-provable-safe shape, its
bounds are propagated: `q[i]` is checked against a **snapshot** of `p`'s own
absolute `[lo, hi)` range, taken at the moment of assignment — not against
`p`'s live declaration re-evaluated at the access (the way a direct access
through `p` itself works). This is deliberately a *simpler*, cheaper
guarantee than real dataflow analysis: no kill-set tracking over `p` or the
variables its bounds formula references, because the range is captured as
two absolute addresses (in a pair of compiler-generated hidden locals) the
instant the assignment runs, and later mutation of `p`/`n` is irrelevant to
an already-taken snapshot.

A pointer local `q` (declared with or without an initializer) is a
propagation *candidate* iff it is unchecked (`checked_kind == CHECKED_NONE`
— a pointer with its own declared bounds never needs propagation). Every
candidate is classified into exactly one of three outcomes:

- **NONE** — never assigned a checked-rooted value anywhere in the
  function, or address-taken outside the hidden temporary `to_assign()`
  builds for `q += k`/`q++`/`q--`. No propagation at all; `q[i]` is never
  checked, same as an ordinary unchecked pointer.
- **FULL** — **every** assignment to `q` reached in the function is
  checked-rooted (its address expression resolves, through `+`/`-`/casts,
  to a declared checked pointer variable or a member access with an
  enforceable bounds form). `q[i]` is checked against a snapshot taken at
  the most recent assignment, unconditionally — the original
  rule, unchanged.
- **OPT** — a *mix* of checked-rooted and non-checked-rooted
  assignments (or a checked-rooted assignment whose own source is itself
  OPT, see "Chained propagation" below). `q[i]` is still checked, but only
  on the paths where the value `q` actually holds at that point came from a
  rooted assignment — see below for how this is decided without any
  control-flow analysis.

Concretely, for the FULL case:

```c
int * [[cccc::array, cccc::count(n)]] p = ...;
int *q = p + 2;   // propagates: snapshot taken here, [p, p + n*4)
q[i];             // checked against the snapshot
q++; q[0];        // still checked -- q++ doesn't re-assign q's snapshot,
                  // and doesn't need to: the range is absolute, not
                  // relative to q's current value
q = p + 5;        // re-snapshots -- a later access uses the NEW range

int *s = malloc(...);
s = p;     // s's OWN initializer wasn't checked-rooted -- s is OPT, not
           // FULL (see below), NOT poisoned to NONE the way alone
           // would have left it
```

**Path-sensitive propagation via runtime snapshot validity**. An
OPT candidate's `[lo, hi)` snapshot temps are refreshed at **every**
assignment to `q`, not just the checked-rooted ones: a non-checked-rooted
store writes an explicit "invalid" sentinel (`lo = (char*)-1, hi =
(char*)0` — an inverted range no legitimate bound can ever produce) instead
of leaving the temps untouched, and the temps are also seeded with the
sentinel at function entry (covering an uninitialized declaration, a loop
back-edge that hasn't looped yet, or a `goto` past `q`'s own declaration).
The runtime check itself (`CHKRO`, distinct from the `CHKR` a FULL candidate
or a direct declared-checked access still emits) treats the sentinel as a
deliberate no-op rather than a violation.

This makes propagation exact per *executed* path with **no CFG, no join
operator, and no fixpoint over a control-flow graph at all** — a
conservative static join would still leave `q[i]` below unchecked, because
the fact is only live on one incoming edge of the `if`:

```c
int * [[cccc::array, cccc::count(n)]] p = ...;
int *q = malloc(...);
if (c) q = p;
q[i];   // ENFORCED when c took the `q = p` branch, silently unchecked
        // (no trap either way) when it didn't -- exactly per the path
        // actually taken at runtime, decided by which store actually ran

int *r;    // no initializer -- still a candidate
if (c) r = p;
r[i];   // same as `q` above: enforced iff `c` was true THIS run

int *s = p;
s[i];       // checked: s's most recent store (the init) was rooted
s = malloc(...);
s[j];       // NOT checked: s's most recent store was not rooted -- ordinary
            // flow-sensitivity/kill-set behavior falls out of the same
            // runtime mechanism for free, no separate tracking needed
```

A FULL candidate gets none of this machinery — no sentinel, no entry init,
plain `CHKR` — so its codegen is byte-for-byte identical to before.

A member is a valid propagation *source* too, so the two features
compose: `int *q = obj.p;` propagates from `obj.p`'s own member bounds.

A self-rooted reassignment is **neutral**: `q = q + 1;` (or any store whose
rhs resolves, via `find_checked_base()`, straight back to `q` itself) neither
poisons `q` nor gets rewritten — the snapshot is an absolute `[lo, hi)` range,
so a self-store cannot invalidate it, the same fact that already lets a plain
`q++`/`q += k` preserve propagation. This is narrowly about the *left-hand
side's own descent*: `q = 1 + q;` is not recognised (`find_checked_base()`
only ever descends `->lhs`, consistent with how `q = 1 + p;` isn't recognised
as checked-rooted either) and still poisons `q`, conservatively. The
carve-out excludes `q`'s own declaration: `int *q = q;` has no prior value of
`q` to have been rooted in anything, so the initializer itself still poisons.

**Chained propagation**. A local that is itself only propagated (never
declared checked) *can* act as a source for a further candidate:

```c
int * [[cccc::array, cccc::count(n)]] p = ...;
int *q = p + 2;   // propagates from p (a declared source)
int *r = q + 1;   // propagates from q (a chained source)
int *s = r + 1;   // propagates from r -- chains to arbitrary depth
```

This is decided by iterating the whole-function classification rule above to
a fixpoint: round 0 accepts only declared-checked sources as a *source*
(today's rule); each later round additionally trusts whichever
candidates survived the *previous* round as sources too, so the
accepted-source set grows monotonically and the pass always terminates
(bounded by the number of candidates in the function, with a defensive round
cap on top). Seeding from the bottom (declared sources only, then growing)
rather than optimistically from the top (assume everything propagates, then
remove failures) is what keeps an unrooted cycle from ever self-validating:
`q = r + 1; r = q + 1;` with no declared root anywhere in the cycle never
propagates (classifies NONE), no matter how many rounds run, because neither
`q` nor `r` is ever a member of any round's *frozen* accepted-source set.

Chaining needs one more soundness argument beyond's own: a chained
source's bounds are runtime values living in its own snapshot temps, not a
recomputable expression, so `q`'s temps must be written before `r`'s
snapshot reads them. That holds because a chain source's own temps are
refreshed at *every* one of its assignments (rooted → real bounds, non-rooted
→ sentinel, per above) plus at function entry, so `q`'s temps are always
in a well-defined state — real or sentinel — by the time any use of `q`,
including `r`'s snapshot read, is reached. `goto` past `q`'s own declaration
is covered by the same phase-B' entry sentinel, not left as a residual (see
below).

**OPT-ness also threads through the chain**: a candidate chained from an
OPT source is itself OPT, even when its own single store is, in isolation,
unconditionally checked-rooted:

```c
int * [[cccc::array, cccc::count(n)]] p = ...;
int *q = malloc(...);
if (c) q = p;      // q is OPT
int *r = q + 1;    // r's own store IS rooted (from q) -- but r must still
                    // be OPT, not FULL, or r[i] would read q's sentinel
                    // through a plain CHKR and TRAP on this correct,
                    // unrooted-path code
r[i];              // enforced iff `c` was true this run, same as q[i] would be
```

The round loop's fixpoint test compares both the survivor count and the
OPT-survivor count between rounds; if the round cap is hit before either
converges, every remaining survivor is conservatively forced to OPT rather
than trusted as FULL — OPT only ever costs precision, so this is the only
sound direction to err in when the cap cuts a chain short.

**`CHKNT` propagation**. The terminator-slot fact travels alongside
the snapshotted `[lo, hi)` range, FULL, OPT, and chained alike: a candidate's
`Obj.checked_prop_nt_elem` is non-zero iff *every* checked-rooted store into
it — declared source directly, or transitively through a chain — is
rooted at an `[[cccc::ntarray]]` source, and every one of those sources
agrees on the same pointee element size. A mix (one `ntarray`-rooted store
and one plain-`array`-rooted store to the same candidate, or two
`ntarray`-rooted stores with different element sizes) disables the guard for
the whole candidate — on the plain-`array` path `hi` is never widened, so
`hi - elem_size` would be the *last real element*, not a terminator slot;
guarding it would falsely trap a legitimate write there:

```c
char * [[cccc::ntarray, cccc::count(n)]] s = ...;
char *q = s;
q[3] = 'x';        // traps: q's hi is a widened ntarray hi (element size 1)

int *r = (int *)s; // cast changes the pointee size -- CHKNT declines here:
r[0] = 1;          // access_size (4) != the source's element size (1)
```

An OPT candidate's own sentinel range (`[(char*)-1, (char*)0)`) no-ops the
same way it already does for `CHKRO` — `CHKNT` already declines whenever
`hi < elem_size`, and the sentinel's `hi == 0` satisfies that unconditionally
for any real element size, so no sentinel-aware variant of `CHKNT` was
needed. Read-modify-write through a propagated pointer (`q[n] += 1`,
`q[n]++`) and the `_Atomic` compare-exchange desugar are both covered too:
`to_assign()` (src/parse_expr.c) desugars an RMW at **parse time**, before this
pass has resolved `q`'s bounds, so it leaves a back-link
(`Node.checked_rmw_mirror`) from the original deref to the synthesized store
node it built; the propagation pass's attach walk follows that link and
mirrors the just-resolved bounds — lo/hi/access-size/nt-terminator/optional
for the non-atomic desugar, hi/access-size/nt-terminator only for the atomic
one (matching's own direct-access split, which has no `lo` to give the
CAS-loop's `ND_CAS` node) — onto the mirror once they're known.

**Assignment-time bounds implication**. The propagation pass above only ever
*widens* trust into a previously-*unchecked* target; a **separate** sibling
pass, `verify_checked_assign_bounds()` (disjoint target set — propagation
candidacy requires `checked_kind == CHECKED_NONE`, so an already-declared-
checked target is never a propagation candidate), now *verifies* trust the
other direction: an `ND_ASSIGN` whose LHS is itself declared checked, and
whose RHS is rooted at a **directly declared-checked** source (not a
chain — v1 boundary, see below), is rewritten so a new `CHKAB` opcode checks
that the source's own bounds imply the target's own declared bounds:

```c
int * [[cccc::array, cccc::count(4)]]  p = ...;   // declared bound: 4
int * [[cccc::array, cccc::count(10)]] q;         // declared bound: 10
q = p;   // now traps: p's bounds (4) don't imply q's own declared bounds (10)
```

`CHKAB rs_val, rs_slo, rs_shi` traps unless `slo <= val && val <= shi`;
codegen emits it twice per checked assignment — once with the target's own
declared `lo`, once with its `hi` — which together enforce
`[dlo, dhi) ⊆ [slo, shi]`. Ordering is the **inverse** of the propagation
pass's own snapshot: the target's declared bounds are self-referencing
(`[q, q + m*sizeof(T))` for a `count(m)` target), so `checked_assign_dst_lo/
hi` are deliberately left as bare expressions, evaluated **after** the
store, to see the just-written value — the source's bounds, by contrast, are
snapshotted into compiler-generated temps **before** the store (the rewrite
is `(temp = source bounds), (q = E)`, mirroring's own before-the-store
snapshot), since the source expression may itself alias or be overwritten by
the assignment.

v1 boundary, deliberate, not a residual gap:

- The RHS must be a **directly declared-checked** source
  (`checked_prop_source_bounds()`'s kind 1) — a-propagated local can
  hold the OPT sentinel range, which would need a sentinel-aware `CHKAB`
  variant; `q = r;` where `r` is itself only a propagation candidate is
  silently skipped, same as any other unrecognised source.
- Only a direct `q = E;` `ND_ASSIGN` is covered — function argument passing
  and return values are not.
- A target with no resolvable bounds form (`CB_NONE`/`bounds(unknown)`) is
  skipped, same rationale as the propagation pass's identically-named source
  exclusion.

Gated on `--checked-pointers` at **parse time**, unlike the rest of the
checked-pointer machinery (which populates its fields unconditionally and
gates only `CHKR`'s *emission* at codegen) — the snapshot temps cost real
stack slots and stores per propagating assignment, only worth paying when
something might enforce them. This has no pragma-ordering caveat: `#pragma
cccc config(checked_pointers = true)` is resolved during preprocessing, a
pass that runs to completion for the whole file before parsing begins, so
the flag is already set by the time any declaration is parsed regardless of
where in the file the pragma appears.
