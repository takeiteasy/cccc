# Pointer Bounds Attributes

Bounds, length and checked-region attributes.

## `single` / `array` / `ntarray` / `count` / `byte_count` / `bounds` (CCCC-specific)

A Checked C-inspired checked-pointer type and bounds-declaration layer
(#770/#482/#483). Unlike every other attribute in this file, these attach in
**post-`*` qualifier position** — the same grammar slot as `const`/
`volatile`/`restrict` — not declspec position, since a C23 attribute in
declspec position would qualify the pointee, not the pointer:

```c
int  * [[cccc::single]]                    p;  // exactly one object
int  * [[cccc::array, cccc::count(n)]]     a;  // n elements from p's own value
char * [[cccc::ntarray, cccc::count(n)]]   s;  // like array, +1 for a terminator slot,
                                                //   which must stay null/all-zero-bytes
                                                //   (`CHKNT`/`CHKNTZ`, #923/#939)
int  * [[cccc::array, cccc::bounds(lo,hi)]] r; // explicit absolute range
```

`__attribute__((...))` and `@single`/`@array`/`@ntarray`/`@count(n)`/
`@byte_count(n)`/`@bounds(lo,hi)` all work too.

`single` rejects all pointer arithmetic on that pointer as a compile error —
this compile-time rule is always on, independent of the runtime flag below.
A bounds form (`count`/`byte_count`/`bounds`) requires `array` or `ntarray`;
it is a compile error on `single` or with no checked kind. Bounds may
reference any other in-scope parameter (including a later one), local, or
global; on a struct/union member (#921), a bounds expression may also
reference a sibling field, resolved relative to whichever instance is
actually accessed — but not an enclosing local or a bit-field sibling
(compile errors). A bounds expression must be side-effect-free (also a
compile error otherwise — it is re-evaluated at every checked access, not
once).

Runtime enforcement (the `CHKR` opcode, plus `CHKNT`/`CHKNTZ` guarding a
non-null/non-all-zero-bytes write into `ntarray`'s widened terminator slot,
#923/#939 — including through a propagated pointer, #943 — and `CHKAB`
verifying a checked-rooted assignment's source bounds imply an
already-declared-checked target's own bounds, #944) is gated behind
`--checked-pointers` /
`#pragma cccc config(checked_pointers = true)` —
opt-in, not part of any `-0`/`-1`/`-2`/`-3` preset. Full reference, including
the bounds-carry-within-an-expression semantics, the whole-function
propagation rule that lets an interior pointer assigned into a plain local
(`int *q = p + k;`) stay checked too (#919), and why this exists
(`--bounds-checks`/`CHKB` has no upper bound at all for a stack or global
array): [safety.md § Checked Pointers](safety-checked-pointers.md#checked-pointers).

Enforcement is VM-only: `-c=native`/`-m`/`-c=generated` warn and drop
`--checked-pointers` rather than enforcing it, but the six attributes are
always parsed, type-checked, and stripped from that output regardless (ABI-
transparent, #482/#488) — see [safety.md § Checked
Pointers](safety-checked-pointers.md#checked-pointers) for the full native/serialized-output
note.

**Bounds-safe interfaces** (#488): annotating a *parameter* with any of
these six attributes needs no separate marker to also serve as a Checked
C-style bounds-safe interface — a checked attribute already imposes zero
restriction on an unchecked caller, so the annotation itself is the
interface. At every call site whose argument is itself declared-checked,
CCCC verifies (via the same `CHKAB` opcode above, under
`--checked-pointers`) that the argument's own bounds imply the parameter's
declared bounds evaluated with that call's actual arguments — closing the
one caller-side gap this layer otherwise left. See [safety.md § Bounds-safe
interfaces](safety-checked-pointers.md#checked-pointers) for the full reference, including
the substitutability rule for non-trivial arguments and the same-TU
callee-body caveat.

**Checked arrays** (#487) extend the same layer to array declarations
directly — `int a _Checked[10]` / `char s _Nt_checked[11]` — so an array's own
extent is checked the same way a checked pointer's `count(n)` is, with no new
opcode. These are keyword-positional suffixes (like `_Checked { ... }`/
`_Unchecked { ... }` below), not attributes, so they have no `[[cccc::...]]`
spelling. See [safety.md § Checked Arrays](safety-checked-types.md#checked-arrays) for the
full reference.

## `assume` / `dynamic` (CCCC-specific)

The entry point into the checked-pointer world: converting an *unchecked*
pointer into a checked one. Like the six attributes above, these attach in
post-`*` qualifier position — but only on a cast's type-name, never on a
declarator:

```c
int *raw = ...;
int * [[cccc::array, cccc::count(n)]] p =
    (int * [[cccc::array, cccc::count(n), cccc::assume]])raw;   // trust the claim
p = (int * [[cccc::array, cccc::count(n), cccc::dynamic]])raw;  // verify it (CHKAB)
```

`assume` takes the claimed checked kind/bounds on trust, with no runtime
check of the claim itself — the documented, deliberate escape hatch, and
the direction `--checked-pointers`' rest of this document calls "always
on regardless of the flag" does not cover: getting an unchecked pointer
into the checked world at all necessarily has to be an unverified act
somewhere. `dynamic` instead verifies, via the existing `CHKAB` opcode
(#944, above), that the claim is implied by the SOURCE's own
declared-checked bounds — a compile error if the source has no verifiable
bounds to check against (`use 'assume' ... or give the source a bounds
declaration`), rather than silently falling back to `--bounds-checks`,
which has no upper bound at all for a stack or global source (see "Why
this exists" in [safety.md § Checked
Pointers](safety-checked-pointers.md#checked-pointers)).

Both require a checked kind (`single`/`array`/`ntarray`); with `array`/
`ntarray` a bounds form is also required (`bounds(unknown)` is accepted for
`assume`, rejected for `dynamic` — there is nothing to verify). Once
converted, every access through the result is an ordinary checked access,
`CHKR`-checked against the claim like any other declared-checked pointer.
Inside a `[[cccc::checked]]`/`[[cccc::unchecked]]` region, these ARE the
sanctioned unchecked→checked conversion — exempt from the region's own cast
ban, no `[[cccc::unchecked]] { ... }` escape needed around them. Gated the
same way as the rest of runtime enforcement: with `--checked-pointers` off,
an annotated cast stays a plain cast. Full reference, including the
compiler-generated-temp desugaring and the v1 scope (a cast expression
only, not a function argument or return value): [safety.md § Checked
Pointers § Bounds casts](safety-checked-pointers.md#checked-pointers).

## `dynamic_check` (CCCC-specific)

```c
__builtin_cccc_dynamic_check(i < n);
_Dynamic_check(i < n);   // Checked-C-compat alias
```

A programmer bounds assertion: traps (via the `CHKDC` opcode) when the
condition is false, under `--checked-pointers`; a no-op otherwise, same
convention as `__builtin_assume`. The condition must be side-effect-free —
it is only evaluated under the flag, and a future static-elision pass may
consume it as a proven fact without evaluating it at all (tracked as a
follow-up). `_Dynamic_check` is recognized positionally (an identifier not
already in scope, immediately followed by `(`), so it never shadows a real
function of that name. Full reference: [safety.md § Checked Pointers §
`dynamic_check`](safety-checked-pointers.md#checked-pointers).

## `checked` / `unchecked` (CCCC-specific)

```c
int  * [[cccc::single]]                    p;  // exactly one object
int  * [[cccc::array, cccc::count(n)]]     a;  // n elements from p's own value
```

is the pointer-*type* half of the checked-pointer layer; `checked`/
`unchecked` is the region half — a way to require every pointer *declared*
within a lexical extent to be one of the checked kinds above, rather than
relying on each declaration to opt in individually:

```c
[[cccc::checked]] void f(void) {
    int * [[cccc::array, cccc::count(n)]] a = ...;   // fine
    int *p = a;                                       // error: unchecked local
    [[cccc::unchecked]] {
        int *raw = (int *)a;                           // fine -- escape hatch
    }
}
```

Attaches to a function definition (its whole body is the region) or to a
compound statement (`{ ... }`, nestable to any depth, innermost wins); also
available as `#pragma cccc checked begin`/`end` (and the `unchecked` pair)
for a positional, file-scope default. `__attribute__((checked))`/
`((unchecked))`, `__checked__`/`__unchecked__`, and `@checked`/`@unchecked`
all work too. Unlike every other attribute on this page, the diagnostics it
enables are **always on** — never gated behind `--checked-pointers` — and
fire in `-c=native` too; the attributes themselves are stripped from
`-E`/`-m`/`-c=generated`/`-c=native` output the same way the six
checked-pointer attributes are. Full reference, including the exact v1 ban
list and the header-contamination exemption: [safety.md § Checked
Regions](safety-checked-regions.md#checked-regions).

---
