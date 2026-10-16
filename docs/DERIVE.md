# Derived Attributes

Attach an attribute to a struct, union, enum or `typedef` and CCCC generates
functions for it at compile time. `#include <cccc/derive.h>` once; no handler
code needed.

```c
#include <cccc/derive.h>

@serialize
@generate_accessors
typedef struct { int x; int y; } Point;

int main(void) {
    Point p = {3, 4};
    char buf[sizeof(Point)];
    Point_serialize(&p, buf);
    return get_x(&p) + get_y(&p) + 35;   // 42
}
```

| Attribute | Applies to | Generates |
|---|---|---|
| `@serialize` | struct/union | `int T_serialize(T *self, void *buf)` |
| `@deserialize` | struct/union | `T T_deserialize(void *buf)` |
| `@enum_to_string` | enum | `const char *T_to_string(T v)` |
| `@enum_from_string` | enum | `T T_from_string(const char *s)` |
| `@generate_getters` | struct/union | `get_<field>(T *self)` per member |
| `@generate_setters` | struct/union | `set_<field>(T *self, <FieldType> value)` per member |
| `@generate_accessors` | struct/union | getters and setters |
| `@generate_constructor` | struct/union | `T T_create(<member>, ...)` |
| `@to_json` | struct | `size_t T_to_json(const T *self, char *buf, size_t cap)` |
| `@from_json` | struct | `int T_from_json(T *out, const char *json)` |
| `@equals` | struct | `_Bool T_equals(const T *a, const T *b)` |
| `@hash` | struct | `size_t T_hash(const T *self)` |
| `@dump` | struct | `void T_dump(const T *self, FILE *out)` |
| `@clone` | struct | `int T_clone(T *out, const T *self)` |
| `@destroy` | struct | `void T_destroy(T *self)` |
| `@owned` / `@owned("n")` | member | marks a pointer member as heap memory for `@clone` / `@destroy` |
| `@builder` | struct | `T_builder` type, `T_builder_new`, a chaining `T_builder_<field>` setter per member, `int T_build(const T_builder *b, T *out)` |
| `@builder_default(expr)` | member | the value `T_builder_new` starts the member with |
| `@builder_required` | member | makes `T_build` fail until the member is set |
| `@compare` | struct | `int T_compare(const T *a, const T *b)` |
| `@bitflags` | enum | `T_set`, `T_clear`, `T_toggle`, `_Bool T_has`, `size_t T_format(T v, char *buf, size_t cap)` |
| `@equals_skip` | member | leaves the member out of `@equals` and `@hash` |
| `@dump_skip` | member | leaves the member out of `@dump` |
| `@json_skip` | member | leaves the member out of `@to_json` / `@from_json` |
| `@json_name("key")` | member | uses `key` as the member's JSON key |

Every attribute accepts both `struct T {...};` and `typedef ... T;`. Applying
one to the wrong kind of type (an enum attribute on a struct, say) is a compile
error.

Using one of these attributes without the include is a compile error that
names `<cccc/derive.h>`. The header adds no code to the program: the handlers
run at compile time, and `-c=native` output carries only the generated
functions.

## Serialization

`@serialize` and `@deserialize` copy a struct to and from a raw byte buffer:

```c
@serialize
@deserialize
typedef struct { int x; int y; } Point;

// Point_serialize returns sizeof(Point).
```

Members are copied one by one, so the buffer needs no particular alignment.
Nested structs, bit-fields and the fields of anonymous struct/union members are
included. Pointer members are copied as raw pointer bytes. Padding is not
written, and `Point_deserialize` zeroes the padding of the value it returns.

The `Serialize(type, expr, buf)` and `Deserialize(type, buf)` builders behind
these attributes are described in [MACROS.md](MACROS.md#macro-standard-library).

## Enum Strings

```c
@enum_to_string
@enum_from_string
typedef enum { RED, GREEN, BLUE } Color;

// Color_to_string(GREEN)   -> "GREEN"
// Color_to_string(99)      -> ""
// Color_from_string("BLUE") -> BLUE
// Color_from_string("nope") -> -1
```

Enumerators that share a value (`enum { A, B, C = 1 }`) map to the first name.

## Accessors and Constructors

`@generate_getters`, `@generate_setters` and `@generate_accessors` publish one
function per member. An optional string literal prefix names them
`<prefix>_get_<field>`, which keeps two structs with a shared field name
apart. `""` means no prefix.

```c
@generate_accessors
struct Point { int x; int y; };
// get_x, get_y, set_x, set_y

@generate_accessors("Size")
struct Size { int x; };
// Size_get_x, Size_set_x
```

| Member | Getter | Setter |
|---|---|---|
| Scalar, struct, pointer | Returns the value | Assigns `value` |
| Array `T a[N]` | Returns `T *` to the first element | Takes `const T *value`, copies the whole array |
| Field of an anonymous struct/union | Same as a direct member | Same as a direct member |

`@generate_constructor` publishes `T_create(field1, field2, ...)`. Array
members take a pointer parameter and are copied; fields of anonymous
struct/union members are flattened into the parameter list:

```c
@generate_constructor
struct Point { int x; int y; };
// struct Point Point_create(int x, int y);
```

## JSON

`@to_json` and `@from_json` also need `#include <cccc/json.h>`, the runtime the
generated functions call (a missing include is a compile error that names it).

```c
#include <cccc/derive.h>
#include <cccc/json.h>

@to_json
@from_json
typedef struct { int id; char name[16]; double scores[2]; } User;

User  u = {7, "ann", {1.5, 2}};
char  buf[128];
size_t n = User_to_json(&u, buf, sizeof buf);
// buf: {"id":7,"name":"ann","scores":[1.5,2]}

User v;
if (User_from_json(&v, buf) != 0) { /* malformed or mismatched */ }
```

`T_to_json` returns the length the whole output needs, like `snprintf`: the
buffer is always NUL-terminated when `cap > 0`, and `cap == 0` only measures.
`T_from_json` returns 0 on success and nonzero otherwise.

| Member | JSON |
|---|---|
| Integers, enums | Number (an enum is its numeric value) |
| `float`, `double` | Number; NaN and infinities are written as `null` |
| `bool` | `true` / `false` |
| `char[N]` | String, escaped; reading fails if it does not fit |
| `T[N]` | Array; reading requires exactly `N` elements |
| Struct, anonymous struct | Object, expanded in place |

A pointer or union member, or a union target, is a compile error that names
the member (`Outer.inner.name`).

Reading is strict: every member must be present with the right type, a number
must fit the member (`256` does not fit an `unsigned char`), and anything after
the closing `}` is an error. Members the struct does not have are skipped, in
any order. Doubles are written with 17 significant digits, so `0.1` comes back
as `0.10000000000000001`.

A prefix names the functions `<prefix>_<Type>_to_json` and
`<prefix>_<Type>_from_json`: `@to_json("app")` on `struct Pair` publishes
`app_Pair_to_json`.

### Skipping and renaming members

`@json_skip` and `@json_name("key")` go on the members, in any declarator
position (`int id @json_name("user_id");`, `@json_skip int a, b;`):

```c
@to_json
@from_json
struct Account {
    int  id @json_name("account_id");
    char owner[8];
    int  secret @json_skip;
};
// {"account_id":7,"owner":"bob"}
```

A skipped member is not written, and `T_from_json` leaves it as it was. Its
key in the input is ignored like any other unknown key. A renamed member is
read and written under the new key only. Two members with the same key
(after renaming) are a compile error, as is `@json_skip` together with
`@json_name`, a key that is empty or longer than 127 bytes, or either attribute
outside a struct member.


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
| Pointer | Address | `%p` |
| `char[N]` | C string, at most `N` bytes | `"..."`, not escaped |
| `T[N]` | Element-wise | `{ 1, 2 }` |
| Struct | Member-wise | `Inner { b = 2 }` |

Members are compared one at a time, so padding is ignored. A `NaN` member makes
`T_equals` false, like `==`. `T_equals(a, b)` implies `T_hash(a) == T_hash(b)`;
the hash is FNV-1a over the compared members, with a string hashed up to its
first NUL.

`@equals_skip` leaves a member out of both `@equals` and `@hash`, and
`@dump_skip` out of `@dump`. Both go on struct members, take no arguments, and
are a compile error anywhere else.

Limitations: an array of arrays (`int grid[2][2]`) works on the VM but does not
compile under `-c=native` yet ([#1631](https://todo.sr.ht/~takeiteasy/cccc/1631)).

`@dump` calls `fputs` and `fprintf`, so the program needs `#include <stdio.h>`
(a missing include is a compile error that names it).

## Builders

`@builder` publishes a `T_builder` type that collects member values one setter
at a time. `T_build` copies them into a `T`, or fails while a required member
is missing:

```c
enum { DEFAULT_PORT = 8080 };

@builder
struct Config {
    char host[32] @builder_required;
    int  port     @builder_default(DEFAULT_PORT);
    int  retries;
};

Config_builder b = Config_builder_new();     // port = 8080, retries = 0
Config_builder_retries(Config_builder_host(&b, "example.org"), 3);

struct Config c;
if (Config_build(&b, &c) != 0) { /* a required member is missing */ }
```

| Function | Behaviour |
|---|---|
| `T_builder_new()` | Zeroes every member, then applies each `@builder_default` |
| `T_builder_<field>(b, value)` | Stores `value` and returns `b`; an array member takes a `const` element pointer and copies the array |
| `T_build(b, out)` | Returns 0 and fills `*out`; returns 1 and leaves `*out` alone while a `@builder_required` member was never set |

`@builder_default(expr)` takes any expression that is valid where the struct is
used (constants, enumerators, `sizeof`). It does not apply to array members,
and a member cannot carry both attributes. A builder is a plain value:
copying it copies the pending state, and the struct sits in its `value` member.
Nested and anonymous struct members get a setter per field, like
`@generate_setters`. A member named `new` is a compile error, because its
setter would clash with `T_builder_new`.

`@builder("app")` publishes `app_Config_builder`, `app_Config_builder_new`,
`app_Config_builder_<field>` and `app_Config_build`.

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

`@clone` needs `@destroy` on the same type. `@clone("app")` and
`@destroy("app")` publish `app_Doc_clone` and `app_Doc_destroy`, and the same
prefix is used to find a pointee's functions. `@owned` goes on pointer
members only; a `void *` member or a pointee with `const` elements cannot
be cloned.

Limitations: two structs that own pointers to each other cannot be cloned
([#1635](https://todo.sr.ht/~takeiteasy/cccc/1635)).

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
element by element. A `NaN` member compares equal to everything. A pointer
member or a union is a compile error. `@compare("app")` publishes
`app_Version_compare`. It fits `qsort` through a one-line wrapper that casts
the `const void *` arguments.

## Bit Flags

`@bitflags` on an enum whose constants are `0` or powers of two generates
flag helpers:

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

A negative constant, or one that is neither `0` nor a power of two, is a
compile error that names it (`@bitflags: constant 'RW' (3) is not a power of
two`), as is a struct or union target. A prefix names the functions
`<prefix>_<Type>_set` and so on: `@bitflags("app")` on `enum Mode` publishes
`app_Mode_set`.

## Writing your own

Handlers for your own attributes use the same mechanism
(`@comptime(attribute("name"))`); see [ATTRIBUTES.md](ATTRIBUTES.md) and
[MACROS.md](MACROS.md). The first handler registered for a name wins, so a
handler you define for a name above, before `#include <cccc/derive.h>` (or
without the include), replaces the built-in one.

A handler for a struct reads the attributes on its members with
`MemberHasAttr` and `MemberAttrArg`; see
[Member attributes](MACROS.md#member-attributes).
