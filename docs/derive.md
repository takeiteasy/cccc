# Derive Attributes

Attributes that generate code for a type: serializers, enum strings, accessors and more.

Attach an attribute to a struct, union, enum or `typedef` and CCCC generates
functions for it at compile time. `#include <cccc/derive.h>` once; no handler
code needed.

```c
#include <cccc/derive.h>

@serialize
@accessors
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
| `@getters` | struct/union | `get_<field>(T *self)` per member |
| `@setters` | struct/union | `set_<field>(T *self, <FieldType> value)` per member |
| `@accessors` | struct/union | getters and setters |
| `@ctor` | struct/union | `T T_new(<member>, ...)` |
| `@to_json` | struct | `size_t T_to_json(const T *self, char *buf, size_t cap)` |
| `@from_json` | struct | `int T_from_json(T *out, const char *json)` |
| `@equals` | struct | `_Bool T_equals(const T *a, const T *b)` |
| `@hash` | struct | `size_t T_hash(const T *self)` |
| `@dump` | struct | `void T_dump(const T *self, FILE *out)` |
| `@clone` | struct | `int T_clone(T *out, const T *self)` |
| `@destroy` | struct | `void T_destroy(T *self)` |
| `@owned` / `@owned("n")` | member | marks a pointer member as heap memory for `@clone` / `@destroy`; `@equals`, `@hash`, `@dump`, `@compare`, `@to_json` and `@from_json` use its content |
| `@default(expr)` | member | the value `T_new` gives the member instead of a parameter |
| `@compare` | struct | `int T_compare(const T *a, const T *b)` |
| `@validate` | struct | `_Bool T_validate(const T *self)` |
| `@clamp` | struct | `void T_clamp(T *self)` |
| `@range(min, max)` | member | the bounds `@validate` checks and `@clamp` enforces |
| `@bitflags` | enum | `T_set`, `T_clear`, `T_toggle`, `_Bool T_has`, `size_t T_format(T v, char *buf, size_t cap)` |
| `@compare_order(N)` | member | compares the member before those without a key, ascending `N` |
| `@compare_desc` | member | reverses the direction `@compare` orders the member in |
| `@equals_skip` | member | leaves the member out of `@equals` and `@hash` |
| `@dump_skip` | member | leaves the member out of `@dump` |
| `@serialize_skip` | member | leaves the member out of `@serialize` / `@deserialize` |
| `@accessor_skip` | member | generates no getter or setter for the member |
| `@ctor_skip` | member | drops the member from `@ctor`'s parameters; it stays zero |
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
these attributes are described in [macros.md](macros-stdlib.md#macro-standard-library).

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

## Skipping Members

`@serialize_skip`, `@accessor_skip` and `@ctor_skip` go on struct
members, take no arguments, and leave the member out of one family of
generated functions:

```c
@serialize
@deserialize
@accessors
@ctor
struct Session {
    int  id;
    char token[16] @serialize_skip @accessor_skip;
    int  hits      @ctor_skip;
};
```

| Attribute | Effect |
|---|---|
| `@serialize_skip` | The member's bytes are not written; `T_deserialize` leaves it zero. `T_serialize` still returns `sizeof(T)` and the other members keep their offsets. The `Serialize` / `Deserialize` builders skip it too. Not valid on a bit-field. |
| `@accessor_skip` | No `get_<field>` or `set_<field>`. |
| `@ctor_skip` | No `T_new` parameter; the member is zero-initialised. |

## Accessors and Constructors

`@getters`, `@setters` and `@accessors` publish one
function per member. An optional string literal prefix names them
`<prefix>_get_<field>`, which keeps two structs with a shared field name
apart. `""` means no prefix.

```c
@accessors
struct Point { int x; int y; };
// get_x, get_y, set_x, set_y

@accessors("Size")
struct Size { int x; };
// Size_get_x, Size_set_x
```

| Member | Getter | Setter |
|---|---|---|
| Scalar, struct, pointer | Returns the value | Assigns `value` |
| Array `T a[N]` | Returns `T *` to the first element | Takes `const T *value`, copies the whole array |
| Field of an anonymous struct/union | Same as a direct member | Same as a direct member |

`@ctor` publishes `T_new(field1, field2, ...)`. Array members take a pointer
parameter and are copied; fields of anonymous struct/union members are
flattened into the parameter list:

```c
@ctor
struct Point { int x; int y; };
// struct Point Point_new(int x, int y);
```

An optional string literal prefix names it `<prefix>_<T>_new`, as for `@to_json`:
`@ctor("app") struct Point {...};` publishes `app_Point_new`.

`@default(expr)` on a member removes its parameter and makes `T_new` assign
`expr` instead; `@ctor_skip` removes it and leaves it zero. A struct whose
members are all defaulted or skipped gets `T_new()` with no parameters.

```c
enum { DEFAULT_PORT = 8080 };

@ctor
struct Config {
    char *host;
    int   port    @default(DEFAULT_PORT);
    char *label   @default("untitled");
    int   retries @ctor_skip;
};
// struct Config Config_new(char *host);
```

`@default` takes one expression that is valid where the struct is used
(constants, enumerators, `sizeof`, string literals). It does not apply to array
members, and a member cannot carry both `@default` and `@ctor_skip`.


## Writing your own

Handlers for your own attributes use the same mechanism
(`@comptime(attribute("name"))`); see [attributes.md](attributes.md) and
[macros.md](macros.md). The first handler registered for a name wins, so a
handler you define for a name above, before `#include <cccc/derive.h>` (or
without the include), replaces the built-in one.

A handler for a struct reads the attributes on its members with
`MemberHasAttr` and `MemberAttrArg`; see
[Member attributes](macros-attributes.md#member-attributes).

[^size]: `@clone` also publishes `unsigned long T_clone_size(void)` (with the same prefix), the size of `T`. A struct that owns a pointer to a still-incomplete `T` calls it to allocate and to step through an `@owned("n")` array, because `sizeof(T)` is not known yet where the owner's functions are generated.

[^forward]: A pointee that is only forward-declared is called through a prototype that is published for it, as for `@clone`, so it can be declared after its owner. An `@owned("n")` array of such a struct is a compile error, because its element size is not known yet.

[^json]: `@to_json` and `@from_json` also publish `void T_json_write(const T *self, __cccc_json_w *w)` and `int T_json_read(T *out, __cccc_json_r *r)` (with the same prefix), the writer and reader behind the public functions. A struct that owns a pointer to a `T` calls them to write or read the pointee, which lets it nest and recurse without a second buffer.

## Guide pages

| Page | Covers |
|---|---|
| [derive-json.md](derive-json.md) | `@to_json` and `@from_json` |
| [derive-values.md](derive-values.md) | Equality, hashing, dumping, owned memory, ordering, validation and bit flags |
