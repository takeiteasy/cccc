# Equality, Hashing, Dumping and Ordering

`@equals`, `@hash`, `@dump` and `@compare` walk a struct's members.

## Equality, hashing and dumping

`@equals`, `@hash` and `@dump` walk the members of a struct. Nested structs and
anonymous structs are expanded in place, so the nested type needs no attribute
of its own. A union target, an anonymous union member, or a named union member
is a compile error.

```c
#include <cccc/derive.h>
#include <stdio.h>

typedef enum { RED, GREEN } Color;

@equals
@hash
@dump
struct Item {
    int   id;
    Color color;
    char  name[8];
    int   cache @equals_skip @dump_skip;
};

struct Item a = {1, GREEN, "ann", 0};
struct Item b = {1, GREEN, "ann", 99};
Item_equals(&a, &b);          // 1: cache is skipped
Item_hash(&a) == Item_hash(&b); // 1
Item_dump(&a, stdout);        // Item { id = 1, color = GREEN, name = "ann" }
```

`@equals` also binds `T_equals` to `==` for `(T, T)`, so `a == b` and `a != b` work on the struct (`!=` is `!(a == b)`; see [operators.md](operators.md)). A hand-written `==` for the same type is a duplicate-operator error.

```c
a == b;  // Item_equals(&a, &b)
a != b;  // !Item_equals(&a, &b)
```

A prefix names the functions `<prefix>_<Type>_equals` (likewise `_hash` and
`_dump`): `@equals("app")` on `struct Pair` publishes `app_Pair_equals`.

| Member | `@equals` / `@hash` | `@dump` |
|---|---|---|
| Integer, enum, `bool`, bit-field | Compared by value | Number; enum by name (the number when no constant matches); `true` / `false` |
| `float`, `double`, `long double` | `==`; `-0.0` equals `0.0` | `%.17g` (`%Lg` for `long double`) |
| Other pointer | Address | `%p` |
| `char[N]` | C string, at most `N` bytes | `"..."`, not escaped |
| `T[N]` | Element-wise | `{ 1, 2 }` |
| Struct | Member-wise | `Inner { b = 2 }` |
| `char *s @owned` | C string; `NULL` equals only `NULL` | `"..."`, or `NULL` |
| `T *a @owned("n")` | Lengths equal, then element-wise | `{ ... }`, or `NULL` |
| `T *p @owned` | Compares `*p` | `*p`, or `NULL` |

Members are compared one at a time, so padding is ignored. A `NaN` member makes
`T_equals` false, like `==`. `T_equals(a, b)` implies `T_hash(a) == T_hash(b)`;
the hash is FNV-1a over the compared members, with a string hashed up to its
first NUL.

`@equals_skip` leaves a member out of both `@equals` and `@hash`, and
`@dump_skip` out of `@dump`. Both go on struct members, take no arguments, and
are a compile error anywhere else.

An `@owned` member (see [Owned memory](derive-owned.md#owned-memory)) is compared, hashed and
dumped by content instead of by address. When its pointee is a struct that
owns memory, or is the struct itself (a linked list), `@equals`, `@hash` and
`@dump` call the pointee's own `T_equals`, `T_hash` or `T_dump`, so put the
attribute on the pointee first; a missing one is a compile error that names it.
The same prefix finds them.[^forward]

`@dump` calls `fputs` and `fprintf`, so the program needs `#include <stdio.h>`
(a missing include is a compile error that names it).


## Ordering

`@compare` generates `int T_compare(const T *a, const T *b)`, returning a
negative, zero or positive value by the first member that differs, in
declaration order:

```c
@compare
struct Version { int major; int minor; char tag[8]; };

struct Version a = {1, 2, "rc"}, b = {1, 10, "rc"};
Version_compare(&a, &b);   // < 0
```

It takes the same members as `@equals` (nested and anonymous structs expanded,
`@equals_skip` leaves one out), so `T_compare(a, b) == 0` exactly when
`T_equals(a, b)` is true. Integers, enums, `bool` and bit-fields order by value,
floats with `<`, `char[N]` as unsigned bytes up to the first NUL, other arrays
element by element. A `NaN` member compares equal to everything. An `@owned`
member orders by content with `NULL` first: a `char *` as unsigned bytes, an
`@owned("n")` array element by element and then by length, any other pointer
through `*p`. A pointer member that is not `@owned`, or a union, is a compile
error. `@compare("app")` publishes
`app_Version_compare`. It fits `qsort` through a one-line wrapper that casts
the `const void *` arguments.

### Choosing the order

`@compare_order(N)` on a member compares it before the members without a key,
in ascending `N` (negative keys are fine). Members with the same key, and the
members without one, keep declaration order. `@compare_desc` reverses the
direction a member is ordered in:

```c
@compare
struct Task {
    char name[16];
    int  due      @compare_order(2);
    int  priority @compare_order(1) @compare_desc;   // highest first
};

// priority (descending), then due, then name
```

The keys order the members of one struct; the members of an anonymous struct
join the struct around it, and a nested struct is ordered by its own keys.
`@compare_desc` on a struct or array member reverses the whole comparison of
that member. Both attributes go on struct members, `@compare_order` takes one
integer constant, and neither can share a member with `@equals_skip`. Only
`@compare` reads them: `@equals`, `@hash` and `@dump` ignore them.
