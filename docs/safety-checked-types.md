# Checked Pointer Types and Arrays

Terminator guarding for `ntarray`, and arrays whose extent is part of their type.

## Why declared bounds

`--bounds-checks` takes its bound from VM-heap allocation metadata, so it has no upper bound for a stack or global array. A checked pointer's bound comes from its declaration, so `--checked-pointers` catches those accesses:

```c
int * [[cccc::array, cccc::count(n)]] a = some_stack_array;
a[n];   // traps under --checked-pointers; --bounds-checks cannot see this
```

## Terminator slot

`ntarray` widens its range by one element so the terminator is writable. The guard then traps a write of anything but zero into that slot.

| Bounds form | Declared end | Terminator slot |
|---|---|---|
| `count(n)` | `p + n*sizeof(T)` | the element starting there |
| `byte_count(n)` | `p + n` | `sizeof(T)` bytes starting there[^bytes] |
| `bounds(lo, hi)` | `hi` | the element starting at `hi` |

```c
int n = 3;
char * [[cccc::ntarray, cccc::count(n)]] s = (char[4]){'a', 'b', 'c', 0};
s[3] = '\0';   // ok
s[3] = 'x';    // traps: non-null write to the terminator slot
```

| Pointee | Trap when the stored value is |
|---|---|
| Integer, pointer, `_BitInt` up to 128 bits | non-zero |
| `float`, `double` | a non-zero bit pattern (`-0.0` traps) |
| `struct`, `union`, wider `_BitInt`, `_Decimal` | any non-zero byte, padding included, checked before the store |
| `long double`, vectors, `_Complex` | not guarded |

```c
struct Option { int a; char b; };
struct Option * [[cccc::ntarray, cccc::count(n)]] tbl = ...;
tbl[n] = (struct Option){0, 0};   // ok
tbl[n] = (struct Option){1, 0};   // traps

double * [[cccc::ntarray, cccc::count(n)]] d = ...;
d[n] = 0.0;    // ok
d[n] = -0.0;   // traps
```

`= {0}` and compound literals zero the whole object first, so they are safe; a struct built member by member with stale padding can trap even when every member reads zero.

Also guarded: read-modify-write (`s[n] += 1`, `s[n]++`), atomic compare-exchange (on the desired value, even if the compare fails), and access through a [propagated pointer](safety-checked-propagation.md#terminator-slot).

A store into a member of the terminator-slot element is guarded the same way, whether a scalar, float, nested, union, array-member or struct-typed member:

```c
Option *[[cccc::ntarray, cccc::count(n)]] tbl = ...;
tbl[n].a = 1;          // traps: non-zero member in the terminator slot
tbl[n].arr[0]++;       // traps
tbl[n].in = (Inner){}; // ok: all-zero bytes
```

A bit-field store is checked after truncation to the field width, so storing `8` into a 3-bit field is fine and storing `9` traps. `long double` members and bit-fields wider than 64 bits are not guarded.[^members]

CCCC does not check that a terminator is *present* in the range: Checked C's `count(n)` is a lower bound, so terminator-free declarations are legal and a scan would also have to read past `hi`.

## Checked arrays

An array declaration whose extent is part of its type. Indexing is checked against it, and decay to a pointer carries the bounds.

```c
int  a _Checked[10];     // a[10] traps under --checked-pointers
char s _Nt_checked[11];  // 10 elements + 1 terminator slot
```

| Spelling | Meaning |
|---|---|
| `_Checked[N]` | `N` elements, checked like `count(N)` |
| `_Nt_checked[N]` | `N` slots, the last reserved for a null terminator (`N` of 0 is an error) |

The suffixes are recognised by position (an identifier followed by `[`), so a variable named `_Checked` elsewhere is unaffected. Locals, globals, statics and struct members are supported. A parameter `int a _Checked[10]` becomes `int * [[cccc::array, cccc::count(10)]] a`. Decay keeps the bounds:

```c
int *q = a;   // q is a propagation candidate rooted at a's extent; q[i] stays checked
```

| Limit | Reason |
|---|---|
| `_Nt_checked` over rows that are already checked arrays (`Str t _Nt_checked[3]` with `typedef char Str _Nt_checked[10]`, or over a `_Checked` typedef row) is an error | The terminator belongs to the innermost row, once |
| A string initializer that fills the last slot (`char s _Nt_checked[3] = "abc"`) is an error | No room is left for the terminator |

## Compound literals

`_Checked` and `_Nt_checked` also go in an abstract declarator, so a compound literal, a `sizeof` or a type name can be a checked array:

```c
int *p = (int _Checked[4]){1, 2, 3, 4};
int  v = (int _Checked[]){5, 6}[1];       // extent 2, from the initializer
```

Indexing the literal is checked against its extent. A variable-length compound literal is not valid C.

## Flexible array members

A flexible array member takes its length from a sibling field:

```c
struct Buf {
    int n;
    int data _Checked[] [[cccc::count(n)]];
};
```

`b->data[i]` traps unless `i < b->n`, and `int *p = b->data` keeps the count. `_Nt_checked[]` works the same way: `count(n)` is the element count and one more slot holds the terminator.

The bound takes the same forms as a checked pointer member:

| Written after `[]` | Bound |
|---|---|
| `[[cccc::count(n)]]` | `n` elements |
| `[[cccc::byte_count(n)]]` | `n` bytes |
| `[[cccc::bounds(data, end)]]` | `[data, end)` |
| `[[cccc::bounds(unknown)]]` | not checked; opts out |

`__attribute__((count(n)))` after the `]` is accepted too. A checked flexible array member without a bound, and a bound on anything but a flexible array member, are errors. `sizeof` is unchanged, and the output of `-m`/`-c=native`/`-c=generated` is an ordinary flexible array member.

## Variable-length checked arrays

A non-constant extent is checked against the length the declaration computed. Changing `n` afterwards does not move the bound, and an extent with side effects runs once.

```c
int n = 5;
int a _Checked[n];
n += 10;
a[4] = 1;    // ok
a[5] = 1;    // traps: the array still has 5 elements

void f(int n, int p _Checked[n]);   // p is count(n) on the decayed pointer
```

Any dimension may vary, and each is checked against its own length. A pointer to a variable-length row keeps that length.

```c
int m = 3, n = 4;
int a _Checked[m][n];
int (*p)[n] = a;
a[1][4] = 1;    // traps: past the row
p[3][0] = 1;    // traps: past the last row
```

A parameter's extent is its pointer's `count(n)`, so it is re-evaluated at each access and must be side-effect free. `_Nt_checked[n]` reserves the last slot for the terminator; at `n == 0` the range is empty and every access traps.

## Multidimensional checked arrays

`_Checked` applies to every dimension: each index is checked against its own extent, and a row decays to a pointer that keeps the row's bounds.

```c
int a _Checked[3][5];
a[2][4] = 1;               // ok
a[1][5] = 1;               // traps: past the row, though inside the object
a[3][0] = 1;               // traps: past the last row
int *r = a[1];             // r is checked against the 5 elements of row 1
int (*p)[5] = a;
p[0][7] = 1;               // traps: a pointer to a row checks its elements against the row's 5 (also when `p` is checked-rooted on only some paths)
```

Three or more dimensions work the same way, and the outer extent may be omitted when an initializer sizes it (`int t _Checked[][5] = {{1}, {2}}`). A string initializer sizes an `_Nt_checked[]` the same way (`char s _Nt_checked[] = "hi"` has 3 slots).

In a multidimensional `_Nt_checked` array only the innermost row holds a terminator slot; the outer dimensions are plain checked arrays:

```c
char s _Nt_checked[3][10];   // 3 rows of 9 characters + a terminator slot each
s[1][9] = 'x';               // traps: non-null store into row 1's terminator slot
typedef char Str _Nt_checked[10];
Str t _Checked[3];           // same shape through a typedef'd row
```

| Case | Behavior |
|---|---|
| Row expression with side effects (`a[i++][j]`) | The outer index is checked; the inner index is not [^rows] |
| Plain outer array of checked rows (`Row a[3]` with `typedef int Row _Checked[5]`) | Each row is checked; the outer index is not |

A plain array inside a [checked region](safety-checked-regions.md#what-is-banned) is an error. Checked arrays serialize as ordinary arrays in `-m`/`-c=native`/`-c=generated` output.

[^rows]: The row expression would be evaluated a second time to build the inner bounds, so the inner check is skipped rather than changing the program's side effects.
[^bytes]: A `byte_count(n)` that is not a multiple of `sizeof(T)` widens by exactly `sizeof(T)` bytes, so the slot is misaligned and no element index reaches it; the guard has nothing to protect there.
[^members]: A `long double` slot is 16 bytes but is stored as an 8-byte double. Internals: [safety-impl.md](safety-impl.md#terminator-guard).
