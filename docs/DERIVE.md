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


## Writing your own

Handlers for your own attributes use the same mechanism
(`@comptime(attribute("name"))`); see [ATTRIBUTES.md](ATTRIBUTES.md) and
[MACROS.md](MACROS.md). The first handler registered for a name wins, so a
handler you define for a name above, before `#include <cccc/derive.h>` (or
without the include), replaces the built-in one.

A handler for a struct reads the attributes on its members with
`MemberHasAttr` and `MemberAttrArg`; see
[Member attributes](MACROS.md#member-attributes).
