# Checked Pointer Bounds Propagation

How a plain pointer inherits the bounds of a checked pointer it is assigned from.

An unchecked local has no bounds of its own, so `q = p + k` would normally go unchecked. When `q` is only ever assigned from checked pointers, CCCC captures `p`'s `[lo, hi)` range as a **snapshot** at the assignment, and checks `q[i]` against it. Later changes to `p` or `n` do not affect an existing snapshot. Propagation needs `--checked-pointers`.

```c
int * [[cccc::array, cccc::count(n)]] p = ...;
int *q = p + 2;   // snapshot of [p, p + n*4) taken here
q[i];             // checked against the snapshot
q++; q[0];        // still checked: the range is absolute
q = p + 5;        // new snapshot; later accesses use it
```

## Which pointers propagate

Each unchecked local falls into one class:

| Class | When | `q[i]` is |
|---|---|---|
| NONE | Never assigned a checked-rooted value, or its address is taken | Unchecked |
| FULL | Every assignment is checked-rooted | Checked against the latest snapshot |
| OPT | A mix of checked-rooted and other assignments | Checked only if the *latest store* was checked-rooted |

A value is checked-rooted when, through `+`, `-` and casts, it resolves to a declared checked pointer or to a member with enforceable bounds (`int *q = obj.p;` propagates from `obj.p`'s bounds).

## Mixed assignments

An OPT pointer is checked per path actually executed, by whichever store ran last:

```c
int * [[cccc::array, cccc::count(n)]] p = ...;
int *q = malloc(...);
if (c) q = p;
q[i];   // checked if c was true this run; unchecked, no trap, otherwise

int *s = p;
s[i];            // checked
s = malloc(...);
s[j];            // not checked: the latest store was not rooted
```

A self-rooted store such as `q = q + 1` leaves the snapshot alone. `q = 1 + q` is not recognised and counts as unrooted.

## Chains

A propagated local can feed another one, to any depth:

```c
int *q = p + 2;   // from p
int *r = q + 1;   // from q
int *s = r + 1;   // from r
```

A pointer chained from an OPT pointer is OPT too, even if its own store is always rooted. A cycle with no declared root (`q = r + 1; r = q + 1;`) never propagates.

## Terminator slot

For `[[cccc::ntarray]]` sources the terminator guard follows the snapshot, if every source agrees on the element size:

```c
char * [[cccc::ntarray, cccc::count(n)]] s = ...;
char *q = s;
q[3] = 'x';          // traps if it hits the terminator slot

int *r = (int *)s;   // cast changes the element size: no terminator guard
r[0] = 1;
```

Mixing `ntarray` and plain `array` sources, or different element sizes, turns the guard off for that pointer. Read-modify-write (`q[n] += 1`, `q[n]++`) and atomic compare-exchange are checked like plain accesses.

## Assignments between checked pointers

Assigning to a pointer that has declared bounds checks that the source's bounds imply the target's:

```c
int * [[cccc::array, cccc::count(4)]]  p = ...;
int * [[cccc::array, cccc::count(10)]] q;
q = p;   // traps: p's 4 elements don't cover q's declared 10
```

| Covered | Not covered |
|---|---|
| `q = E;` where `E` roots in a declared checked pointer, or in a propagated local that is not OPT | A source that is OPT or never rooted |
| Call arguments and `return` values[^args] | A target with `bounds(unknown)` |

[^args]: Arguments to annotated parameters and values returned from a checked return type are checked as assignments to a hidden local; see [safety-checked-interfaces.md](safety-checked-interfaces.md). How the passes work: [safety-impl.md](safety-impl.md#bounds-propagation).
