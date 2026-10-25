# Checked Pointers

A pointer's declaration carries a bounds obligation, and `--checked-pointers` checks every dereference against it.

The bound comes from the **declaration**, not heap metadata, so it works the same on heap, stack and global arrays. It is opt-in and not part of any `-0`…`-3` preset. Declaring the attributes is always parsed and type-checked; [checked regions](safety-checked-regions.md) are a separate, always-on feature.

## Declaring

Attributes go after the `*`, in the same slot as `const` (see [attributes-bounds.md](attributes-bounds.md)).

```c
int  * [[cccc::single]]                    p;  // like Checked C _Ptr<int>
int  * [[cccc::array, cccc::count(n)]]     a;  // _Array_ptr<int> : count(n)
char * [[cccc::ntarray, cccc::count(n)]]   s;  // _Nt_array_ptr<char>
```

| Attribute | Meaning |
|---|---|
| `single` | Exactly one object; arithmetic is a compile error; a dereference is checked against `[p, p + sizeof(T))`, which also checks for NULL |
| `array` | Points into an array-like region; a bounds form gives its extent |
| `ntarray` | Like `array`, with one extra element for a terminator that may only be written as null |
| `count(n)` | Valid for `n` elements: `[p, p + n*sizeof(T))` |
| `byte_count(n)` | Valid for `n` bytes: `[p, p + n)` |
| `bounds(lo, hi)` | Explicit absolute range |
| `bounds(unknown)` | Trust escape hatch: typed, but never range-checked |

A bounds form needs `array` or `ntarray`; on `single` or with no kind it is an error. `array` or `ntarray` with no bounds form is legal but unchecked outside a [checked region](safety-checked-regions.md), where it is an error.

## Bounds expressions

A bounds expression may name any in-scope parameter, local or global, including a later parameter. It is re-evaluated at every checked access, so it must be side-effect-free: `count(i++)` and `count(c ? i++ : 3)` are errors, `count(n ?: 8)` is fine.

For `count` and `byte_count` the lower bound is the pointer's live value, so reassigning `p` moves both the base and the lower bound. `(p + k)[i]` still checks against `p`'s declared bounds.

A prototype's bounds are resolved at the declaration, so a bad bound is an error there, with or without `--checked-pointers`. A bound on a prototype, typedef or function pointer may name sibling parameters and globals declared earlier. The bounds are used at every call ([safety-checked-interfaces.md](safety-checked-interfaces.md)).

### On struct members

A member's bounds may name a sibling field in either order, or an earlier global, resolved against the instance being accessed:

```c
struct S {
    int n;
    int * [[cccc::array, cccc::count(n)]] p;
};
// s.p[i] is checked against s.n; t.p[i] against t.n
```

`s.p[i]`, `sp->p[i]`, `(&s)->p[i]` and `(*sp).p[i]` all work. An object expression with side effects (`f()->p[i]`) gets no check, because instrumenting it would evaluate `f()` an extra time. A runtime index (`arr[k].p[i]`) is evaluated once per access.

| Restriction | Reason |
|---|---|
| A bound naming a local of the scope the struct is defined in is an error | The struct can outlive the local |
| A bound naming a bit-field sibling is an error | Nothing extracts a bit-field value there |

`single` and `bounds(unknown)` work on members as on variables.

## Example

```c
int main(void) {
    int n = 5;
    int stack_arr[5] = {1, 2, 3, 4, 5};
    int * [[cccc::array, cccc::count(n)]] a = stack_arr;
    return a[5];   // out of bounds; --bounds-checks cannot see this
}
```

```
$ ./cccc --checked-pointers test_checked_bounds.c

========== CHECKED BOUNDS VIOLATION ==========
Checked-pointer access out of declared bounds
Address:       0x14fffffdc
Access size:   4 bytes
Declared bounds: [0x14fffffc8, 0x14fffffdc)
PC: 0xe5 (offset: 229)
================================================
```

A non-null write into an `ntarray` terminator slot reports a terminator violation:

```c
char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
s[3] = 'x';
```

```
========== CHECKED TERMINATOR VIOLATION ==========
Non-null store into a [[cccc::ntarray]] terminator slot
Terminator slot: [0x157ffffeb, 0x157ffffec)
Value stored:    120
====================================================
```

Checks are also enforced in `-c=native` and `-m` output ([safety-checked-interfaces.md](safety-checked-interfaces.md#native-output)).

## More

| Topic | Page |
|---|---|
| Plain locals inheriting bounds | [safety-checked-propagation.md](safety-checked-propagation.md) |
| Annotated parameters, bounds casts, `dynamic_check` | [safety-checked-interfaces.md](safety-checked-interfaces.md) |
| Terminator guard, checked arrays | [safety-checked-types.md](safety-checked-types.md) |
| Regions that ban unchecked pointers | [safety-checked-regions.md](safety-checked-regions.md) |
