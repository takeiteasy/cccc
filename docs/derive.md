# Derive Attributes

Attributes that generate code for a type: serializers, enum strings, accessors and more.

Attach an attribute to a struct, union, enum or `typedef` and CCCC generates
functions for it at compile time. No include and no handler code needed: the
first built-in attribute in the source file loads `<cccc/derive.h>`, the
JSON ones also load `<cccc/json.h>`, and `@from_string` and `@text` load
`<cccc/text.h>`.[^implicit]

```c
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
| `@serialize` | struct/union | `int T_serialize(T *self, void *buf)`, `unsigned long T_serialize_size(const T *self)` |
| `@deserialize` | struct/union | `T T_deserialize(void *buf)`, `int T_deserialize_into(T *out, const void *buf, unsigned long len)`, `unsigned long T_deserialize_read(T *out, void *buf)` |
| `@to_string` | enum | `const char *T_to_string(T v)` |
| `@to_string` | struct | `size_t T_to_string(const T *self, char *buf, size_t cap)` |
| `@from_string` | enum | `T T_from_string(const char *s)` |
| `@from_string` | struct | `int T_from_string(T *out, const char *s)` |
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
| `@json`, `@text`, `@binary`, `@lifecycle`, `@hashable`, `@ordered` | struct/union | a pair of the above, see [Bundles](#bundles) |
| `@owned` / `@owned("n")` | member | marks a pointer member as heap memory for `@clone` / `@destroy`; `@equals`, `@hash`, `@dump`, `@compare`, `@to_json` and `@from_json` use its content |
| `@default(expr)` or `T m = expr;` | member, function parameter | the value `T_new` gives the member instead of a parameter; the value of an omitted argument |
| `@compare` | struct | `int T_compare(const T *a, const T *b)` |
| `@validate` | struct | `_Bool T_validate(const T *self)` |
| `@clamp` | struct | `void T_clamp(T *self)` |
| `@range(min, max)` | member | the bounds `@validate` checks and `@clamp` enforces |
| `@bitflags` | enum | `T_set`, `T_clear`, `T_toggle`, `_Bool T_has`, `size_t T_format(T v, char *buf, size_t cap)` |
| `@compare_order(N)` | member | compares the member before those without a key, ascending `N` |
| `@compare_desc` | member | reverses the direction `@compare` orders the member in |
| `@equals_skip` | member | leaves the member out of `@equals` and `@hash` |
| `@dump_skip` | member | leaves the member out of `@dump` and a struct's `@to_string` |
| `@to_string_skip` | member | leaves the member out of a struct's `@to_string` |
| `@serialize_skip` | member | leaves the member out of `@serialize` / `@deserialize` |
| `@accessor_skip` | member | generates no getter or setter for the member |
| `@ctor_skip` | member | drops the member from `@ctor`'s parameters; it stays zero |
| `@json_skip` | member | leaves the member out of `@to_json` / `@from_json` |
| `@json_name("key")` | member | uses `key` as the member's JSON key |

Every attribute accepts both `struct T {...};` and `typedef ... T;`. Applying
one to the wrong kind of type (an enum attribute on a struct, say) is a compile
error.

A struct defined together with variables takes the attribute after the tag or
the closing brace, where it belongs to the type. A leading attribute belongs to
the variables and is a compile error:

```c
struct Point { int x; int y; } @ctor origin;   // Point_new, and origin
struct @equals Pair { int a; int b; } pair;    // Pair_equals, and pair
```

`--no-implicit-derive` turns the automatic load off; an attribute used without
`#include <cccc/derive.h>` is then a compile error that names the header. The
header adds no code to the program: the handlers run at compile time, and
`-c=native` output carries only the generated functions.

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
included. Pointer members that are not [`@owned`](derive-owned.md) are copied as
raw pointer bytes. Padding is not written, and `Point_deserialize` zeroes the
padding of the value it returns.

### Owned members

A struct with [`@owned`](derive-owned.md) members also writes their content after
the `sizeof(T)` fixed bytes, so the buffer holds the strings, arrays and
pointees themselves. `T_serialize_size` gives the size to allocate, and
`T_serialize` returns the bytes written:

```c
@serialize @deserialize @destroy
struct Config { int n; char *host @owned; int *ports @owned("n"); };

unsigned long size = Config_serialize_size(&cfg);
char *buf = malloc(size);
Config_serialize(&cfg, buf);
struct Config copy = Config_deserialize(buf);   // allocates host and ports
Config_destroy(&copy);
```

`T_deserialize` allocates the owned members; release them with `@destroy`. It
needs `#include <stdlib.h>` and `<string.h>`, trusts the buffer and does not
report allocation failure. For a buffer that may be truncated or hostile, use
`T_deserialize_into(&out, buf, len)`: it checks every length against `len`,
returns `0` on success or `1` on a malformed buffer or failed allocation, and
on failure frees what it allocated and leaves `*out` untouched. It needs
`@destroy` on the struct and its pointees. The buffer uses the host's byte order, so it is not
portable between machines.[^binary] A pointee that owns memory needs its own
`@serialize` / `@deserialize` declared first, unless it is only forward-declared
(see [forward declarations](derive-owned.md)). `@serialize_skip` leaves an owned member out too.

The `Serialize(type, expr, buf)` and `Deserialize(type, buf)` builders behind
these attributes are described in [macros.md](macros-stdlib.md#macro-standard-library).

## Strings

`@to_string` and `@from_string` convert an enum to its constant's name and back,
and a struct to text and back. See [derive-text.md](derive-text.md).

## Skipping Members

`@serialize_skip`, `@accessor_skip`, `@ctor_skip` and `@to_string_skip` go on struct
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
| `@to_string_skip` | A struct's `@to_string` leaves the member out. `@dump_skip` does the same and also hides it from `@dump`. |

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

The parameters are named after the members, so `T_new` takes keyed arguments
(see [Parameter defaults](#parameter-defaults)):

```c
@ctor struct Win { int w; int h @default(24); int d; };
Win_new(.d = 2, .w = 1);   // w = 1, h = 24, d = 2
```

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

### Parameter defaults

`@default(expr)` also marks a function parameter. Every parameter after a
defaulted one needs a default too, and the attribute is written on one
declaration only; the definition inherits it.

```c
int area(int w @default(80), int h @default(24));

area();          // area(80, 24)
area(.h = 10);   // area(80, 10)
```

A call that leaves out trailing arguments takes their defaults, and
`.name = value` gives an argument by parameter name.

| Rule | Example |
|---|---|
| Positional arguments come first, then keyed ones | `area(7, .h = 9)` |
| Any named parameter can be keyed, with or without a default | `f(.a = 1)` |
| A key names a parameter once, and not one already given positionally | `area(1, .w = 2)` is an error |
| Keyed arguments are reordered into parameter order | `area(.h = 1, .w = 2)` is `area(2, 1)` |
| A parameter with neither argument nor default is "too few arguments", naming the first such parameter | `f()` for `int f(int a)` reports `a` |

Defaults and keys apply to a direct call of a named function and to a method
call. A call through a function pointer, `(*f)()` or `(&f)()`, or through a
block needs every argument, and keys are an error there. A `@comptime` function
takes neither. Names for keys come from the first
declaration that writes them, so `int f(int, int);` followed by
`int f(int a, int b) {...}` accepts `f(.a = 1, .b = 2)`.

The default is written in the declaration and evaluated at each call that
omits it, in the caller's function but with file-scope names only, so a local
of the caller never changes its meaning.[^default-eval] On a variadic function,
defaults fill only the fixed parameters.

A default can name an earlier parameter, and sees the value the call gives it:

```c
int box(int w, int h @default(w * 2));

box(10);          // box(10, 20)
box(10, 5);       // box(10, 5)
box(.w = 3);      // box(3, 6)
```

Each earlier argument is evaluated once, however often the default names it.
A keyed or defaulted earlier argument counts as its resolved value.

| Rule | Example |
|---|---|
| Only earlier parameters are visible | `int f(int a @default(b), int b @default(0))` is an error |
| A parameter does not name itself | `int f(int a @default(a))` is an error |
| Constants declared in the parameter list are not visible | `int f(enum { A, B } v @default(B))` is an error |
| Names resolve against the declaration the default is written on | `int f(int x, int y); int f(int a, int b @default(a));` |
| Names declared before the declaration work, enum constants included | `int paint(enum Color c @default(GREEN))` |
| A checked pointer parameter cannot be named | `int g(int *[[cccc::array, cccc::count(n)]] p, int n, int q @default(p[0]))` is an error at the call |
| The call needs a function to run in | `int g = box(1);` is an error at file scope, `sizeof(box(1))` is fine |

The attribute is readable through `ParamHasAttr` and `ParamAttrArg`, see
[macros-attributes.md](macros-attributes.md#parameter-attributes).

[^default-eval]: The default's tokens are parsed again at the call, so a
    compound literal gets fresh storage per call and `@default(tick())` runs
    once per call. Keyed arguments are evaluated in parameter order, not the order
    written. When a default names an earlier parameter, the arguments up to the
    last one it names are stored in temporaries first, left to right, and the
    call reads them back. A literal, or a plain local in a call with no side
    effects, is read in place of its temporary. An argument for a checked
    pointer parameter stays in the call, where its bounds are verified, so it
    is evaluated after those.

### Member initializers

`T m = expr;` in the struct body means `T m @default(expr);`. It applies only
through `T_new`: `S s;`, `S s = {0};`, `calloc` and `memset` leave the member
as C does, and the C emitted for `-c=native` has no initializers.

```c
@ctor struct Config {
    char *host;
    int   port    = 8080;
    int   retries : 3 = 2;
};
// struct Config Config_new(char *host);
```

| Case | Result |
|---|---|
| struct without `@ctor` | compile error at the `=` |
| array member (`int a[2] = {1, 2};`) | compile error |
| member also marked `@ctor_skip` or `@default(...)` | compile error |
| member of a named nested struct | needs `@ctor` on that struct |


## Bundles

A bundle runs a pair of attributes on one type, as `@accessors` runs
`@getters` and `@setters`:

| Bundle | Same as |
|---|---|
| `@json` | `@to_json` + `@from_json` |
| `@text` | `@to_string` + `@from_string` |
| `@binary` | `@serialize` + `@deserialize` |
| `@lifecycle` | `@clone` + `@destroy` |
| `@hashable` | `@equals` + `@hash` |
| `@ordered` | `@equals` + `@compare` |

```c
@json("app")
@lifecycle
struct Config { int port; char *host @owned; };
// app_Config_to_json, app_Config_from_json, Config_clone, Config_destroy
```

A bundle takes the optional prefix its parts take. Listing a bundle together
with one of its parts (`@json @to_json`), or two bundles that share a part
(`@hashable @ordered` both publish `T_equals`), is a compile error that names
the duplicate function. Write `@hashable @compare` for both.

## Several input files

A struct derived in a header that two inputs include links as one definition: each derived function exists once in the program. Two different structs of one name in different inputs still stop with `redefinition of 'T_equals'`.

## Writing your own

Handlers for your own attributes use the same mechanism
(`@comptime(attribute("name"))`); see [attributes.md](attributes.md) and
[macros.md](macros.md). The first handler registered for a name wins, so a
handler you define for a name above, before `#include <cccc/derive.h>` (or
without the include), replaces the built-in one.

A handler for a struct reads the attributes on its members with
`MemberHasAttr` and `MemberAttrArg`; see
[Member attributes](macros-attributes.md#member-attributes).

[^implicit]: Only the source file named on the command line is scanned, and only for attributes written as `@name`, `[[cccc::name]]` or `__attribute__((name))`. A header that uses a derive attribute includes `<cccc/derive.h>` itself, and `<cccc/json.h>` or `<cccc/text.h>` where it needs them. An explicit include next to the automatic load is a no-op.

[^size]: `@clone` also publishes `unsigned long T_clone_size(void)` (with the same prefix), the size of `T`. A struct that owns a pointer to a still-incomplete `T` calls it to allocate and to step through an `@owned("n")` array, because `sizeof(T)` is not known yet where the owner's functions are generated.

[^binary]: Each owned member follows the fixed bytes in declaration order: a presence byte (`0` for `NULL`), then a string as an `unsigned long` length and its bytes, an array as its elements (the length member is in the fixed bytes), or one object. A pointee that owns memory is written through its own `T_serialize`; `T_deserialize_read` is the reader it uses.

[^forward]: A pointee that is only forward-declared is called through a prototype that is published for it, as for `@clone`, so it can be declared after its owner. Every derive that follows `@owned` members also steps through an `@owned("n")` array of such a struct with `T_clone_size`, so the pointee needs `@clone`.

[^json]: `@to_json` and `@from_json` also publish `void T_json_write(const T *self, __cccc_json_w *w)` and `int T_json_read(T *out, __cccc_json_r *r)` (with the same prefix), the writer and reader behind the public functions. A struct that owns a pointer to a `T` calls them to write or read the pointee, which lets it nest and recurse without a second buffer.

## Guide pages

| Page | Covers |
|---|---|
| [derive-json.md](derive-json.md) | `@to_json` and `@from_json` |
| [derive-text.md](derive-text.md) | `@to_string` and `@from_string` |
| [derive-equality.md](derive-equality.md) | `@equals`, `@hash`, `@dump`, `@compare` |
| [derive-owned.md](derive-owned.md) | `@owned`, `@clone`, `@destroy` |
| [derive-validate.md](derive-validate.md) | `@range`, `@validate`, `@clamp`, `@bitflags` |
