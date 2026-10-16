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

## Writing your own

Handlers for your own attributes use the same mechanism
(`@comptime(attribute("name"))`); see [ATTRIBUTES.md](ATTRIBUTES.md) and
[MACROS.md](MACROS.md). The first handler registered for a name wins, so a
handler you define for a name above, before `#include <cccc/derive.h>` (or
without the include), replaces the built-in one.
