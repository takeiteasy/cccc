# Checked Pointer Types and Arrays

`ntarray`, guarded pointee types and checked arrays.

**Why this exists**: `CHKB` (`--bounds-checks`) derives its bound from
`AllocHeader.size`, which only exists for VM-heap allocations — it has no
upper bound at all for a stack or global array. Checked pointers close that
gap: their bound comes from the declaration, not allocation metadata, so
`--checked-pointers` catches out-of-bounds accesses on stack and global
arrays that `--bounds-checks` structurally cannot (it is enforced by the
VM's `CHKR` opcode on every checked dereference).

```c
int * [[cccc::array, cccc::count(n)]] a = some_stack_array;
a[n]; // traps under --checked-pointers; CHKB cannot catch this at all
```

**Terminator invariant**. `ntarray`'s bounds widening
(`docs/safety.md`'s `ntarray` row above) makes a one-element terminator slot
writable at the end of the declared range, but it says nothing about what
may legally be written there. Two opcodes close the soundly-checkable part
of that gap: `CHKNT` traps a store of a **non-zero** value into the widened
terminator slot, keyed off the same `[lo, hi)` `CHKR` already computed for
the access (`addr == hi - elem_size && val != 0`); `CHKNTZ` traps a
store of a **non-all-zero-bytes** value for pointees whose store never
passes through a single value register. Both run under the same
`--checked-pointers` flag as `CHKR`, no separate opt-in.

**Which pointee types are guarded**:

| Pointee | Opcode | How |
|---|---|---|
| Integer (`char`/`int`/.../`_BitInt`), pointer | `CHKNT` | value register, compared to 0 |
| `float`, `double` | `CHKNT` | value's raw bits transferred into an integer register first (`FR2R`/`FR2R_F32`), then compared to 0 -- so `-0.0`/`-0.0f` (a non-zero bit pattern) traps, even though it compares equal to `0.0` |
| `struct`, `union`, wide `_BitInt`, `_Decimal32/64/128` | `CHKNTZ` | source bytes scanned for any non-zero byte, **before** the underlying `memcpy` runs -- the slot is never actually clobbered when this traps. All bytes of the object must be zero, including padding: `= {0}` and compound-literal initializers zero the whole object first (`ND_MEMZERO`), so those patterns are safe, but a struct assembled member-by-member with stale/uninitialized padding can trap even when every named member reads as its own zero value |
| `long double`, vector, `_Complex`, anything else | *(unguarded)* | `long double`'s widened terminator slot is 16 bytes (`sizeof`), but the actual store is an 8-byte flat-double `FSTR` -- a guard would assert bytes it never inspected, a false assurance rather than an honest gap. Vectors and `_Complex` are excluded for the same reason: no opcode reads their full stored representation into a scannable register or byte range today |

`CHKNTZ` only guards a **whole-object** store through the pointer itself
(`tbl[n] = (Option){...}`); writing a single field of the terminator slot in
place (`tbl[n].a = 1;`) is not guarded at all -- see "Known coverage gaps"
below.

```c
struct Option { int a; char b; };
struct Option * [[cccc::ntarray, cccc::count(n)]] tbl = ...;
tbl[n] = (struct Option){0, 0}; // ok -- all-zero-bytes terminator
tbl[n] = (struct Option){1, 0}; // traps under --checked-pointers -- CHKNTZ

double * [[cccc::ntarray, cccc::count(n)]] a = ...;
a[n] = 0.0;  // ok
a[n] = -0.0; // traps -- non-zero bit pattern, even though -0.0 == 0.0
```

The widening rule is the same regardless of which bounds form declared the
range: **the terminator slot is the `elem_size` bytes beginning at the
declared end of the range.**

| Bounds form | Declared end | Terminator slot |
|---|---|---|
| `count(n)` | `p + n*sizeof(T)` | `[p + n*sizeof(T), p + (n+1)*sizeof(T))` |
| `byte_count(n)` | `p + n` | `[p + n, p + n + sizeof(T))` |
| `bounds(lo, hi)` | `hi` | `[hi, hi + sizeof(T))` |

`byte_count`'s widening is byte-granular, not element-granular: a
`byte_count(n)` whose `n` is not a multiple of `sizeof(T)` still widens by
exactly `sizeof(T)` bytes, so the resulting slot is not aligned to an
element boundary and no element-indexed access (`s[i]`) can ever land on it
— `CHKR` still admits the extra bytes, but `CHKNT` has nothing reachable to
guard there. This is a pre-existing property of declaring a `byte_count`
that doesn't divide evenly by the element size, not a new gap.

```c
int n = 3;
char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
s[3] = '\0'; // ok -- terminator slot, still null
s[3] = 'x';  // traps under --checked-pointers -- non-null write to the slot
```

`CHKNT` deliberately does **not** attempt the other half of the invariant —
verifying a null terminator is actually *present* somewhere in the declared
range. That is not soundly checkable from the declaration alone: Checked C's
`count(n)` on an `_Nt_array_ptr` is a **lower** bound on the valid extent, not
an assertion that a terminator exists within it —
`_Nt_array_ptr<char> s : count(0)` is a legal, terminator-free declaration,
and `count(5) = "hello world"` (no null in the declared range at all) is
conforming too. A check that scans `[lo, hi)` for a null and traps on absence
would false-positive on both. Worse, actually *locating* the real terminator
requires reading past `hi` — exactly the unbounded read the bounds
declaration exists to prevent. So presence validation is not implemented;
`CHKNT` only ever looks at the one slot the widening itself makes legal to
write.

Read-modify-write on the terminator slot is covered as directly as
plain assignment. `s[n] += 1` and `s[n]++` desugar (`to_assign()`,
`src/parse_expr.c`) to `tmp = &s[n]; *tmp = *tmp + 1` — the synthesized `*tmp`
store deref now carries the same `checked_bounds_lo/hi`/
`checked_nt_terminator` as `s[n]` itself, so `CHKNT` traps it exactly like a
direct `s[n] = 1` (this includes the float/double bit-pattern-transfer path,
e.g. `a[n] += 1.0`). An `_Atomic`-qualified `ntarray` element's RMW takes a
separate CAS-loop desugar (`compare_and_swap` under the hood) instead of a
plain store; that path gets its own `CHKNT` emission ahead of the `ACAS`
opcode, checking the CAS's *desired* value — so an attempted non-null write
still traps even on a CAS iteration that would have failed the compare.
`CHKNTZ` has no CAS-loop counterpart: C has no `+=`/`++`/`--` operator on a
struct/union/wide-`_BitInt` value, so `to_assign()`'s CAS-loop desugar never
applies to those pointees — only plain assignment (`tbl[n] = ...`) or the
non-atomic RMW desugar (`tmp = &A; *tmp = *tmp op B`, which lowers to an
ordinary `CHKNTZ`-guarded store, same as any other) can reach one. `_Decimal`
does support `+=`/`++`/`--`; its `_Atomic` CAS-loop path is unaffected by
this ticket either way — `ND_CAS`'s own type check
(`src/codegen_expr.c`, `sz` must be 1/2/4/8 and not `is_flonum`) governs whether
an `_Atomic _Decimal` RMW is accepted at all, independent of `CHKNTZ`.

**`CHKNT`/`CHKNTZ` propagate across assignment**. `q =
s;` where `s` is an `[[cccc::ntarray]]` source makes `q[n] = 'x'` trap
through `q` exactly the same way `s[n] = 'x'` traps directly — FULL, OPT,
and chained propagation candidates alike, through the
read-modify-write and `_Atomic` compare-exchange desugars, and for
struct-typed sources too. See "Bounds propagation across assignment" above
for the full mechanism (element-size matching, the mixed-source conflict
rule, and the RMW back-link) — propagation keys off element size only, so
the same size-match rule now also decides `CHKNTZ` attachment for a
propagated aggregate access.

Known coverage gaps, left as follow-up work rather than built into this pass:

- **`CHKNT`/`CHKNTZ` do not attempt the presence half of the invariant** —
  see above.
- **A member-wise write into a `struct`/`union` terminator slot is
  unguarded**. `CHKNTZ` only ever sees a whole-object store
  through the pointer itself (`tbl[n] = (Option){...}`, an `ND_DEREF`
  lvalue) — the same shape `CHKNT` has always required. Writing a single
  field of the terminator slot in place (`tbl[n].a = 1;`) stores through an
  `ND_MEMBER` lvalue instead, which `checked_nt_terminator` is never
  attached to (`set_checked_deref_bounds()`/`checked_prop_attach_scan()`
  only stamp it on the outer `ND_DEREF`), so neither `CHKNT` nor `CHKNTZ`
  fires — only `CHKR`'s ordinary range check runs. This gap has no scalar
  analogue: an integer/pointer/float pointee has no members, so this access
  shape doesn't exist for `CHKNT`. It is new specifically because `CHKNTZ`
  extends the guard to pointee types that *do* have members.


## Checked Arrays

Everything above is *pointer*-rooted — a checked pointer's bounds come from
its own declaration, checked at every dereference. A checked array closes the
one gap that leaves: an array declaration whose own extent becomes part of
its checked type, so indexing it is `CHKR`-checked against that extent, and
decaying it into a plain pointer carries those bounds along through the
bounds-propagation pass above.

```c
int  a _Checked[10];     // 10 elements; a[10] traps under --checked-pointers
char s _Nt_checked[11];  // 10 real elements + 1 terminator slot
```

Two keyword suffixes, recognized *positionally* — an identifier immediately
followed by `[`, the same convention `_Checked { ... }`/`_Unchecked { ... }`
(above) use for region blocks — so neither name is reserved and a variable or
function actually named `_Checked`/`_Nt_checked` elsewhere is unaffected:

| Spelling | Meaning |
|---|---|
| `_Checked[N]` | An `N`-element array; indexing is checked against `[a, a + N*sizeof(T))`, same as a checked pointer's `count(N)`. |
| `_Nt_checked[N]` | Like `_Checked[N]`, but the last of the `N` declared elements is reserved as a null-terminator slot — the checked (non-terminator) range is `[a, a + (N-1)*sizeof(T))`, widened by one element for the terminator exactly as `[[cccc::ntarray]]` widens a checked pointer's range. Writing that slot is permitted only with a null value (`CHKNT`/`CHKNTZ`, same as a checked pointer). `_Nt_checked[0]` is a compile error — there is nothing left once the terminator slot is reserved. |

Local, global, `static`-local, and struct/union member checked arrays are all
supported, with the same member-relative resolution a checked pointer
member has. A checked array **parameter** adjusts to a checked pointer at the
same array-to-pointer parameter adjustment an ordinary array parameter goes
through: `void f(int a _Checked[10])` is equivalent to
`void f(int * [[cccc::array, cccc::count(10)]] a)`.

**Decay carries bounds.** Assigning a checked array to a plain, unchecked
pointer local is an ordinary declared-checked source for the bounds
propagation pass described above: `int *q = a;` makes `q` a FULL propagation
candidate rooted at `a`'s own declared extent, so `q[i]` stays checked
exactly as if `q` had been assigned from a checked pointer.

v1 scope, both deliberate, not residual gaps: a checked array's extent must
be a compile-time constant (no VLA form — `_Checked[n]` for a non-constant
`n` is a compile error, since there is no fixed extent to check against), and
a checked array is single-dimension only (`_Checked[3][5]` is a compile
error — a silently-unchecked checked array would be worse than refusing it
outright). A checked region's ban list is unchanged by checked arrays — a
plain, non-`_Checked` array declaration inside a checked region stays legal,
same as it was before.

Like every checked-pointer attribute, `_Checked[N]`/`_Nt_checked[N]` leave no
trace in `-m`/`--dump-expanded`/`-c=native`/`-c=generated` output — a checked
array serializes as an ordinary array declaration, ABI-transparent.
