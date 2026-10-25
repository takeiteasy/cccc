# Derive: Text

`@to_string` and `@from_string` convert an enum or a struct to text and back.

```c
#include <cccc/derive.h>
#include <cccc/text.h>
#include <stdio.h>

@text
typedef struct { int x; char name[8]; } Point;

Point p = {3, "a\"b"}, q;
char  buf[64];
Point_to_string(&p, buf, sizeof buf);   // Point { x = 3, name = "a\"b" }
Point_from_string(&q, buf);             // 0, q equals p
```

| Attribute | Applies to | Generates |
|---|---|---|
| `@to_string` | enum | `const char *T_to_string(T v)` |
| `@to_string` | struct | `size_t T_to_string(const T *self, char *buf, size_t cap)` |
| `@from_string` | enum | `T T_from_string(const char *s)` |
| `@from_string` | struct | `int T_from_string(T *out, const char *s)` |
| `@text` | enum, struct | both of the above |

An optional prefix names the functions `<prefix>_<T>_to_string` and
`<prefix>_<T>_from_string`: `@text("app")` on `enum Color` publishes
`app_Color_to_string`.

## Enums

`T_to_string(v)` returns the constant's name, or `""` when no constant matches.
`T_from_string(s)` returns the constant named `s`, or `-1`. Enumerators that
share a value map to the first name.

```c
@text
typedef enum { RED, GREEN, BLUE } Color;

Color_to_string(GREEN);      // "GREEN"
Color_from_string("BLUE");   // BLUE
Color_from_string("nope");   // -1
```

## Structs

The struct form writes the text [`@dump`](derive-equality.md#equality-hashing-and-dumping)
prints into a buffer. It returns the length the whole text needs, like
`snprintf`, and always NUL-terminates when `cap > 0`:

```c
Point_to_string(&p, NULL, 0);   // the size to allocate, minus the NUL
```

`T_from_string` reads that text back into `*out` and returns `0`. It returns
`1` for malformed or trailing text, a missing, repeated or unknown member, or a
value that does not fit; `*out` is then left untouched. The struct form needs
`#include <stdio.h>` to write and `#include <cccc/text.h>` to read, and
rejects unions.

| Text | Reads as |
|---|---|
| `Point { x = 3 }` | the type name is optional and not checked: `{ x = 3 }` works |
| `x = 3, y = 4` | members in any order, each exactly once |
| `{ 1, 2 }` | an array, exactly as many elements as it holds |
| `"a\"b"` | a string with the escapes below |
| `GREEN` or `42` | an enum constant name, or its number when no constant matched |
| `inf`, `-inf`, `nan` | floating-point specials |
| `NULL` | an `@owned` pointer that is `NULL` |

Members left out with `@dump_skip` or `@to_string_skip` are not in the text and
are zero after `T_from_string`. A pointer member that is not
[`@owned`](derive-owned.md) is a compile error for `@from_string`; mark it
`@to_string_skip` to leave it out. An `@owned` member is read into memory
`T_from_string` allocates, so release it with `@destroy`. A pointee that is only
forward-declared can be read if it also has `@clone`; `@to_string` rejects it.

## Strings

`@dump` and `@to_string` escape `"` and `\` with a backslash, newline, tab and
carriage return as `\n` `\t` `\r`, and any other control byte as three octal
digits (`\001`). Bytes of 0x80 and above are written as they are. A `char[N]`
holds up to `N` bytes; a string that fills it exactly has no NUL, and a shorter
one is padded with zeros. `\000` is rejected.

## Bundle

`@text` is `@to_string` followed by `@from_string`; see
[Bundles](derive.md#bundles).
