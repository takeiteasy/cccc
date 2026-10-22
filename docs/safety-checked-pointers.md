# Checked Pointers

Bounds obligations carried by a pointer's declaration.

## `--checked-pointers` — Checked-pointer bounds checking

- Enforces the `[[cccc::single/array/ntarray]]` + `count()`/`byte_count()`/
  `bounds()` attributes declared on a pointer type (always parsed and
  type-checked regardless of this flag; see
  [Checked Pointers](#checked-pointers) below for the full reference)
- Unlike `--bounds-checks` above, the bound comes from the pointer's own
  **declaration**, not from heap allocation metadata — so it works
  uniformly on heap, stack, and global-array pointers, not just the heap
- CHKR opcode validates `addr != 0 && lo <= addr && addr + size <= hi` on
  every checked dereference, where `[lo, hi)` is recomputed from the
  declaration at each access
- Also enforced in `-c=native` and `-m` output (see
  [Checked Pointers](#checked-pointers))
- Not part of any `-0`/`-1`/`-2`/`-3` safety preset; opt in explicitly
- [Checked regions](safety-checked-regions.md#checked-regions) (`[[cccc::checked]]`/
  `[[cccc::unchecked]]`, `#pragma cccc checked/unchecked begin/end`) are a
  **separate, always-on** frontend feature — their diagnostics fire
  regardless of this flag, unlike `CHKR` itself

### Example

```c
// test_checked_bounds.c - Checked-pointer bounds checking example
int main(void) {
    int n = 5;
    int stack_arr[5] = {1, 2, 3, 4, 5};
    int * [[cccc::array, cccc::count(n)]] a = stack_arr;
    int x = a[5]; // Out of bounds -- and CHKB cannot catch this at all,
                  // since stack_arr has no AllocHeader to derive a bound from.
    return x;
}
```

```bash
$ ./cccc --checked-pointers test_checked_bounds.c

========== CHECKED BOUNDS VIOLATION ==========
Checked-pointer access out of declared bounds
Address:       0x14fffffdc
Access size:   4 bytes
Declared bounds: [0x14fffffc8, 0x14fffffdc)
PC: 0xe5 (offset: 229)
================================================
```

```c
// test_checked_nt.c - Checked-pointer terminator-invariant example
int main(void) {
    int n = 3;
    char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
    s[3] = 'x'; // Non-null write into the widened terminator slot
    return 0;
}
```

```bash
$ ./cccc --checked-pointers test_checked_nt.c

========== CHECKED TERMINATOR VIOLATION ==========
Non-null store into a [[cccc::ntarray]] terminator slot
Address:         0x157ffffeb
Terminator slot: [0x157ffffeb, 0x157ffffec)
Value stored:    120
PC: 0xed (offset: 237)
====================================================
```


## Checked Pointers

A Checked C-inspired spatial-safety layer: a pointer's
declaration carries a *bounds* obligation, and every dereference through it
is checked at runtime against that declaration.

**Attribute syntax.** The attributes attach in post-`*` qualifier position,
the same grammar slot as `const`/`volatile`/`restrict` — not in declspec
position, which would qualify the pointee rather than the pointer:

```c
int  * [[cccc::single]]                    p;  // ~ Checked C _Ptr<int>
int  * [[cccc::array, cccc::count(n)]]     a;  // ~ _Array_ptr<int> : count(n)
char * [[cccc::ntarray, cccc::count(n)]]   s;  // ~ _Nt_array_ptr<char>
```

`__attribute__((...))` and the `@`-prefix spellings work too
(`@array`/`@count(n)`/etc.).

| Attribute | Meaning |
|---|---|
| `single` | Points to exactly one object; all pointer arithmetic on it is a compile error; deref is implicitly range-checked against `[p, p + sizeof(T))`, which is also a NULL check |
| `array` | Points into an array-like region; a bounds form (below) declares its extent |
| `ntarray` | Like `array`, but widens the checked range by one element (`sizeof(T)` bytes at the declared end of the range) for a null-terminator slot, under all three bounds forms; writing that slot is permitted **only with a null value** |
| `count(n)` | The pointer is valid for `n` elements from its own current value: `[p, p + n*sizeof(T))` |
| `byte_count(n)` | Like `count(n)` but `n` is a byte count, not an element count: `[p, p + n)` |
| `bounds(lo, hi)` | Explicit absolute range `[lo, hi)`, independent of the pointer's own value |
| `bounds(unknown)` | Trust escape hatch: the type is checked, but no runtime range check is ever emitted for it |

A bounds form (`count`/`byte_count`/`bounds`) requires `array` or `ntarray`;
it is a compile error on `single` (which has its own implicit range) or with
no checked kind at all. `array`/`ntarray` with no bounds form at all is
legal but unchecked — nothing to enforce, same effect as `bounds(unknown)`.

**Bounds declarations** can reference any other in-scope
parameter, local, or global — including a *later* parameter of the same
function (`count(n)` before `int n` in the parameter list). A bounds
expression must be side-effect-free (`count(i++)` is a compile error): it is
**re-evaluated at every checked access**, not just once at the declaration,
so a side effect would run once per access instead of once. The check
applies inside a ternary's branches too — `count(c ? i++ : 3)` is rejected
just like `count(i++)` — but a *pure* ternary is accepted, including GNU
elvis (`count(n ?: 8)`, which desugars without a compiler temp specifically
so a pure elvis bounds expression stays side-effect-free). A
prototype-only declaration (`void f(int * [[cccc::array, cccc::count(n)]]
p, int n);`, no body) leaves its bounds unresolved at the DECLARATION site —
there is nothing to check there, and this is correct, not an error — but its
bounds are still consumed at every CALL site: see "Bounds-safe interfaces"
below.

**Struct/union member bounds**. A bounds expression on a member may
reference a *sibling* member — in either textual order, the same "may name a
later declaration" rule bounds on a parameter already have — or a global
declared before the struct:

```c
struct S {
    int n;
    int * [[cccc::array, cccc::count(n)]] p;  // count(n) -- a sibling field
};
```

`n` in `count(n)` resolves relative to whichever struct/union *instance* is
actually being accessed (`s.p[i]` checks against `s.n`; `t.p[i]` against
`t.n`) — not to a single fixed scope the way a local's or parameter's bounds
are. Every access spelling reaches the same member-relative base:
`s.p[i]`, `sp->p[i]`, `(&s)->p[i]`, `(*sp).p[i]`. An object expression with
side effects (`f()->p[i]`) is declined — no check is emitted — permanently,
not merely because of an evaluation-count concern that a smarter
implementation could remove: the hoist (below) rewrites the *bounds
expressions* a check reads, not the access itself, so `f()->p[i]`'s own
access still calls `f()` once regardless of whether a check is emitted.
Instrumenting it would therefore call `f()` an extra time just to build the
hoisted temp, and a `--checked-pointers` build introducing an extra
evaluation of a side-effecting expression over a default build is not an
acceptable trade for the check. A non-trivial, side-effect-free object
expression (a runtime index, e.g. `k` in `arr[k].p[i]`) is evaluated exactly
once per checked access, into a compiler-generated temp shared by every
bound it feeds — not once per bound the way a bare local's re-cloned
expression is (free either way, since that folds to a stack-frame offset).
The same once-per-access-site treatment also applies to bounds propagation
across assignment and assignment-time bounds implication (below): a
non-trivial object expression in `q = arr[k].p;` or `arr[k].p = src;` is
evaluated once for that whole assignment's bounds, not once for propagation
alone. Two restrictions specific to member bounds:

- An identifier resolving to a **local** of whatever scope the struct
  happens to be *defined* in (as opposed to a sibling member or a global) is
  a compile error — the struct type can outlive that local, so trusting it
  would be unsound.
- A sibling that is itself a **bit-field** is a compile error — nothing
  extracts a bit-field's value through the member substitution this
  performs.

`[[cccc::single]]` and `bounds(unknown)` on a member work exactly as they do
on a variable, with no member-specific restriction.

- `(p + k)[i]` checks against `p`'s own declared bounds — arithmetic
  *within a single expression* still resolves back to the declared pointer.
- For `count(n)`/`byte_count(n)` (but not `bounds(lo, hi)`, which is already
  absolute), the *lower* bound at each access is the pointer's own live
  value at that point in the program — so reassigning `p` itself (not a
  different variable) and then accessing through it uses the new value, both
  as the base address and as the lower bound.
