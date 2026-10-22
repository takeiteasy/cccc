# Value Semantics Derives

Equality, hashing, dumping, owned memory, ordering, validation and bit flags.

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

An `@owned` member (see [Owned memory](#owned-memory)) is compared, hashed and
dumped by content instead of by address. When its pointee is a struct that
owns memory, or is the struct itself (a linked list), `@equals`, `@hash` and
`@dump` call the pointee's own `T_equals`, `T_hash` or `T_dump`, so put the
attribute on the pointee first; a missing one is a compile error that names it.
The same prefix finds them.[^forward]

`@dump` calls `fputs` and `fprintf`, so the program needs `#include <stdio.h>`
(a missing include is a compile error that names it).

## Owned memory

`@clone` and `@destroy` copy and free the pointer members you mark `@owned`.
Unmarked members are copied as they are, so a plain pointer is shared.
`#include <stdlib.h>` is required.

```c
#include <cccc/derive.h>
#include <stdlib.h>

@clone @destroy
struct Doc {
    char *title @owned;          // heap C string
    int   n;
    int  *words @owned("n");     // heap array of n ints
    char *cursor;                // borrowed, copied as a pointer
};

struct Doc copy;
if (Doc_clone(&copy, &doc) == 0) {
    /* copy owns its own title and words */
    Doc_destroy(&copy);
}
```

| Member | `@clone` | `@destroy` |
|---|---|---|
| `char *s @owned` | Copies the string | `free`s it |
| `T *p @owned` | Copies one `T` | `free`s it |
| `T *a @owned("n")` | Copies `n` elements, `n` an integer member of the same struct | `free`s the array |

`T_clone` returns 0 on success. If an allocation fails it frees what it had
already copied, returns 1 and leaves `*out` untouched. A NULL member stays
NULL. `T_destroy` sets each freed member to NULL, so calling it twice is
harmless, and does not free `self`.

When the pointee is itself a struct that owns memory, `T_clone` calls the
pointee's `_clone` and `_destroy`, so put `@clone @destroy` on the pointee
first. A struct can own pointers to itself (a linked list). Nested and
anonymous structs, and arrays of them, are walked in place. An `@owned` member
whose pointee needs a missing function is a compile error that names it
(`needs Inner_clone`).

A pointee that is only forward-declared can be declared after its owner, which
lets two structs own pointers to each other:

```c
struct Leaf;

@clone @destroy struct Branch {
    struct Leaf *leaves @owned("n");
    int          n;
};

@clone @destroy struct Leaf {
    struct Branch *up @owned;
};
```

The pointee must get `@clone` and `@destroy` later in the program; a missing
one is an undefined-function error when the program links.[^size]

`@clone` needs `@destroy` on the same type. `@clone("app")` and
`@destroy("app")` publish `app_Doc_clone` and `app_Doc_destroy`, and the same
prefix is used to find a pointee's functions. `@owned` goes on pointer
members only; a `void *` member or a pointee with `const` elements cannot
be cloned.

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

## Validation and clamping

`@range(min, max)` on an integer, enum or bit-field member, or on an array of
them, sets its inclusive bounds. `@validate` checks them; `@clamp` enforces them.

```c
typedef enum { LOW = 1, MID, HIGH } Level;

@validate @clamp struct Cfg {
    int      port  @range(1, 65535);
    unsigned retry @range(0, MAX_RETRY);
    Level    lvl   @range(LOW, HIGH);
    int      bits : 4 @range(-3, 3);
    short    grid[2][3] @range(0, 100); // every element
    char     name[16];                  // no @range: not checked
};

struct Cfg c = {.port = 70000};
Cfg_validate(&c);   // 0
Cfg_clamp(&c);      // port is now 65535
Cfg_validate(&c);   // 1
```

| Generated | Behaviour |
|---|---|
| `_Bool T_validate(const T *self)` | 1 when every `@range` member is within its bounds, else 0 |
| `void T_clamp(T *self)` | moves a member below `min` up to `min` and one above `max` down to `max` |

`@validate("P")` and `@clamp("P")` publish `P_T_validate` and `P_T_clamp`. The
bounds are integer constant expressions, so enum constants, `sizeof` and casts
work. Members of an anonymous struct are checked with the struct around them. An
unsigned 64-bit member takes bounds up to `ULLONG_MAX`.

A nested struct member (or an array of them) is checked by calling the nested
struct's own `T_validate` / `T_clamp`, found by the struct's tag with the same
prefix, so the nested struct needs the attribute and is declared first:

```c
@validate @clamp struct Inner { int a @range(0, 9); };
@validate @clamp struct Outer {
    struct Inner one;
    struct Inner many[2];    // Outer_validate checks all three
};
```

A nested struct with no `@range` members, and a named union member, is skipped.

A compile error is raised for:

- `@range` that is not on a struct member, does not have exactly two integer
  constants, or has `min` above `max`
- `@range` on a `_Bool`, floating-point, pointer, `char` array or struct member
- a bound the member's type cannot hold (`unsigned char c @range(0, 300)`,
  `unsigned lo : 3 @range(0, 10)`, a negative `min` on an unsigned member)
- `@validate` or `@clamp` on a union or an anonymous union member
- `@validate` or `@clamp` on a struct with a nested struct that has `@range`
  members but no `T_validate` / `T_clamp` published

Only `@validate` and `@clamp` read `@range`; without them it has no effect.

## Bit Flags

`@bitflags` on an enum whose constants are `0`, powers of two, or
combinations of those powers generates flag helpers:

```c
@bitflags
typedef enum { NONE = 0, R = 1, W = 2, X = 4 } Perm;

Perm p = Perm_set(NONE, R);      // R
p = Perm_toggle(p, X);           // R|X
p = Perm_clear(p, R);            // X
Perm_has(p, X);                  // 1

char buf[32];
Perm_format(R | X, buf, sizeof buf);   // "R|X"
```

| Function | Result |
|---|---|
| `T_set(v, f)` / `T_clear(v, f)` / `T_toggle(v, f)` | `v` with the bits of `f` set / cleared / flipped |
| `T_has(v, f)` | Nonzero when every bit of `f` is set in `v` (always true for `f == 0`) |
| `T_format(v, buf, cap)` | Writes the set flags as `R\|W`, in declaration order |

`T_format` returns the length the whole output needs and always NUL-terminates
when `cap > 0`, like `snprintf`; `cap == 0` only measures. Bits with no
constant print as `0x..` after the names (`R|0x40`). A value of `0` prints as
the `0` constant's name, or `0` when there is none. Constants that share a
value print once, under the first name.

A composite constant is an alias for several flags. `T_format` prints
composites with the most bits first (ties in declaration order), so the alias
replaces the names it covers:

```c
@bitflags
typedef enum { R = 1, W = 2, X = 4, RW = R | W, RWX = R | W | X } Perm;

Perm_format(R | W | X, buf, sizeof buf);       // "RWX"
Perm_format(R | W, buf, sizeof buf);           // "RW"
Perm_format(R | X, buf, sizeof buf);           // "R|X"
Perm_format(R | W | 0x40, buf, sizeof buf);    // "RW|0x40"
```

A negative constant is a compile error that names it, as is a constant with a
bit no power-of-two constant names (`@bitflags: constant 'RX' (5) has bits no
single-bit constant names (0x4)`) and a struct or union target. A prefix names the functions
`<prefix>_<Type>_set` and so on: `@bitflags("app")` on `enum Mode` publishes
`app_Mode_set`.
